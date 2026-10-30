#include <arch/clock.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/user/address_wait.h>
#include <kernel/user_memory.h>

#define ADDRESS_WAIT_BUCKETS 64

/* May nest inside process lifetime exclusion, and outside scheduler queues.
 * Mapping leases cover enrolment, but their guard is never held here. */
struct address_wait_bucket {
  struct spinlock lock;
  struct address_wait_record *head, *tail;
};

static struct address_wait_bucket buckets[ADDRESS_WAIT_BUCKETS];

static bool valid_word(uint64_t address, uint64_t word_size)
{
  return (word_size == THREAD_ADDRESS_WORD32_BYTES ||
      word_size == THREAD_ADDRESS_WORD64_BYTES) && !(address & (word_size - 1));
}

static size_t bucket_index(struct process *process, uintptr_t address,
    uint64_t incarnation, size_t word_size)
{
  uintptr_t hash = ((uintptr_t)process >> 4) ^ (address >> 2) ^ incarnation ^ word_size;
  hash ^= hash >> 16;
  return hash % ADDRESS_WAIT_BUCKETS;
}

static bool matches(const struct address_wait_record *record, struct process *process,
    uintptr_t address, uint64_t incarnation, size_t word_size)
{
  return record->process == process && record->address == address &&
      record->incarnation == incarnation && record->word_size == word_size;
}

/* The caller saves any traversal state before this call. Notification can make
 * the task consume its record immediately; do not touch it afterwards. */
static void detach(struct address_wait_bucket *bucket, struct address_wait_record *record,
    struct address_wait_record *previous, enum address_wait_result result)
{
  KASSERT(record->linked && record->result == ADDRESS_WAIT_PENDING);
  if (previous) {
    previous->next = record->next;
  } else {
    KASSERT(bucket->head == record);
    bucket->head = record->next;
  }
  if (bucket->tail == record) {
    bucket->tail = previous;
  }
  record->next = NULL;
  record->linked = false;
  record->result = result;
  vm_wait_detach(record->process->address_space);
  task_address_wait_wake(record);
}

void address_wait_cancel(struct address_wait_record *record, size_t index,
    enum address_wait_result result)
{
  KASSERT(index < ADDRESS_WAIT_BUCKETS && result != ADDRESS_WAIT_PENDING);
  struct address_wait_bucket *bucket = &buckets[index];
  spin_lock(&bucket->lock);
  if (record->linked) {
    KASSERT(record->bucket == index);
    struct address_wait_record *previous = NULL;
    struct address_wait_record *cursor = bucket->head;
    while (cursor != record) {
      KASSERT(cursor);
      previous = cursor;
      cursor = cursor->next;
    }
    detach(bucket, record, previous, result);
  }
  spin_unlock(&bucket->lock);
}

enum call_status address_wait_leased(const struct thread_address_wait *request,
    struct user_buffer_lease *lease)
{
  if (!valid_word(request->address, request->word_size) ||
      (request->word_size == THREAD_ADDRESS_WORD32_BYTES && request->expected > UINT32_MAX)) {
    user_buffer_lease_release(lease);
    return CALL_BAD_REQUEST;
  }
  struct process *process = lease->process;
  uint64_t incarnation;
  if (!vm_address_wait_token(process->address_space, request->address,
      request->word_size, &incarnation)) {
    user_buffer_lease_release(lease);
    return CALL_BAD_BUFFER;
  }
  size_t index = bucket_index(process, request->address, incarnation, request->word_size);
  struct address_wait_bucket *bucket = &buckets[index];
  spin_lock(&bucket->lock);
  /* Leased, token-validated user RAM; SMAP is off, and enabling it needs a user-access window here. */
  uint64_t value = request->word_size == THREAD_ADDRESS_WORD32_BYTES ?
      __atomic_load_n((uint32_t *)(uintptr_t)request->address, __ATOMIC_RELAXED) :
      __atomic_load_n((uint64_t *)(uintptr_t)request->address, __ATOMIC_RELAXED);
  enum call_status status = CALL_OK;
  struct address_wait_record *record = NULL;
  if (value != request->expected) {
    status = CALL_WOULD_BLOCK;
  } else if (request->deadline != THREAD_ADDRESS_NO_DEADLINE &&
      request->deadline <= arch_monotonic_ns()) {
    status = CALL_TIMED_OUT;
  } else {
    record = task_address_wait_prepare();
    record->address = request->address;
    record->incarnation = incarnation;
    record->word_size = request->word_size;
    record->bucket = index;
    if (!task_address_wait_publish(record)) {
      status = CALL_ENDPOINT_CLOSED;
    } else {
      if (bucket->tail) {
        bucket->tail->next = record;
      } else {
        bucket->head = record;
      }
      bucket->tail = record;
      record->linked = true;
      vm_wait_enroll(process->address_space);
    }
  }
  spin_unlock(&bucket->lock);
  user_buffer_lease_release(lease);
  if (!record || status != CALL_OK) {
    return status;
  }

  task_address_wait_sleep(record, request->deadline);
  spin_lock(&bucket->lock);
  KASSERT(!record->linked && record->result != ADDRESS_WAIT_PENDING);
  enum address_wait_result result = record->result;
  spin_unlock(&bucket->lock);
  switch (result) {
  case ADDRESS_WAIT_WOKEN: return CALL_OK;
  case ADDRESS_WAIT_TIMED_OUT: return CALL_TIMED_OUT;
  case ADDRESS_WAIT_WITHDRAWN: return CALL_BAD_BUFFER;
  case ADDRESS_WAIT_STOPPED: return CALL_ENDPOINT_CLOSED;
  default: KASSERT(false);
  }
  return CALL_BAD_REQUEST;
}

enum call_status address_wake_leased(const struct thread_address_wake *request, uint64_t *woken,
    const struct user_buffer_lease *lease)
{
  *woken = 0;
  if (!valid_word(request->address, request->word_size) || !request->maximum) {
    return CALL_BAD_REQUEST;
  }
  struct process *process = lease->process;
  uint64_t incarnation;
  if (!vm_address_wait_token(process->address_space, request->address,
      request->word_size, &incarnation)) {
    return CALL_BAD_BUFFER;
  }
  struct address_wait_bucket *bucket =
      &buckets[bucket_index(process, request->address, incarnation, request->word_size)];
  spin_lock(&bucket->lock);
  struct address_wait_record *previous = NULL;
  struct address_wait_record *record = bucket->head;
  while (record && *woken < request->maximum) {
    struct address_wait_record *next = record->next;
    if (matches(record, process, request->address, incarnation, request->word_size)) {
      ++*woken;
      detach(bucket, record, previous, ADDRESS_WAIT_WOKEN);
    } else {
      previous = record;
    }
    record = next;
  }
  spin_unlock(&bucket->lock);
  return CALL_OK;
}

void address_wait_withdraw(struct vm_space *space, uintptr_t base, size_t bytes)
{
  KASSERT(bytes && bytes <= UINTPTR_MAX - base);
  for (size_t i = 0; i < ADDRESS_WAIT_BUCKETS; ++i) {
    struct address_wait_bucket *bucket = &buckets[i];
    spin_lock(&bucket->lock);
    struct address_wait_record *previous = NULL;
    struct address_wait_record *record = bucket->head;
    while (record) {
      struct address_wait_record *next = record->next;
      if (record->process->address_space == space &&
          record->address >= base && record->address < base + bytes) {
        detach(bucket, record, previous, ADDRESS_WAIT_WITHDRAWN);
      } else {
        previous = record;
      }
      record = next;
    }
    spin_unlock(&bucket->lock);
  }
}
