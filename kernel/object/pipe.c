#include <kernel/object/forwarder.h>
#include <abi/file.h>
#include <abi/pipe.h>
#include <abi/wait.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/pipe.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <kernel/user/wait.h>
#include <kernel/wait.h>

#define PIPE_END_COUNT 2
/* Queued values per pipe: an implementation limit, not a contract. */
#define PIPE_VALUE_SLOTS 4

/* A value sits after exactly `position` stream bytes: every byte written before
 * it was published, so it is at the head once the reader consumed them. */
struct pipe_value {
  struct capability_grant grant;
  uint64_t position;
};

struct pipe_pair {
  atomic_bool locked;
  struct pipe_end reader, writer;
  struct task_wait_link *readers, *writers;
  size_t head, count;
  bool reader_closed, writer_closed;
  size_t remaining_ends;
  /* Values: fixed at creation; written counts every byte ever accepted. */
  bool values_enabled, receiving;
  uint64_t written;
  size_t value_head, value_count, value_admitting;
  struct pipe_value values[PIPE_VALUE_SLOTS];
  uint8_t data[PIPE_CAPACITY];
};

/* Grants dropped under the pipe lock, released after unlocking. */
struct pipe_discard {
  struct capability_grant grants[PIPE_VALUE_SLOTS];
  size_t count;
};

/* IF=0; pipe lock may nest scheduler locks. Only bounded kernel-buffer copies
 * run while held, never allocation, user access or a context switch. The pair
 * outlives either end without retaining a reference to the opposite endpoint. */
static void lock_pipe(struct pipe_pair *pair)
{
  while (atomic_exchange_explicit(&pair->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_pipe(struct pipe_pair *pair)
{
  atomic_store_explicit(&pair->locked, false, memory_order_release);
}

/* Caller retains either endpoint, keeping both embedded source records alive. */
static void notify_pair(struct pipe_pair *pair)
{
  readiness_source_notify(&pair->reader.readiness);
  readiness_source_notify(&pair->writer.readiness);
}

/* Called under the pipe lock. Remove every published pointer before waking. */
static void wake_all(struct task_wait_link **queue)
{
  struct task_wait_link *record = *queue;
  *queue = NULL;
  while (record) {
    struct task_wait_link *next = record->next;
    struct task_wait *wait = record->wait;
    record->next = NULL;
    record->wait = NULL;
    task_wait_wake(wait);
    record = next;
  }
}

/* Called under the pipe lock after interruptible sleep. */
static void detach_waiter(struct task_wait_link **queue, struct task_wait_link *record)
{
  if (!record->wait) {
    return;
  }
  while (*queue != record) {
    KASSERT(*queue);
    queue = &(*queue)->next;
  }
  *queue = record->next;
  record->next = NULL;
  record->wait = NULL;
}

static uint64_t consumed(const struct pipe_pair *pair)
{
  return pair->written - pair->count;
}

static bool value_at_head(const struct pipe_pair *pair)
{
  return pair->value_count && pair->values[pair->value_head].position == consumed(pair);
}

/* Under the pipe lock. A value being received stays with its receiver. */
static void drop_values(struct pipe_pair *pair, struct pipe_discard *discard)
{
  size_t keep = pair->receiving ? 1 : 0;
  while (pair->value_count > keep) {
    size_t last = (pair->value_head + pair->value_count - 1) % PIPE_VALUE_SLOTS;
    discard->grants[discard->count++] = pair->values[last].grant;
    pair->values[last] = (struct pipe_value){0};
    --pair->value_count;
  }
}

static void release_discard(struct pipe_discard *discard)
{
  for (size_t i = 0; i < discard->count; ++i) {
    capability_grant_release(&discard->grants[i]);
  }
  discard->count = 0;
}

/* Storage references keep the pair alive without keeping a direction open.
 * Last-reader closure drops queued values; a writer closing leaves them. */
static void close_end(struct pipe_end *end, struct pipe_discard *discard)
{
  struct pipe_pair *pair = end->pair;
  if (end->reader) {
    pair->reader_closed = true;
    drop_values(pair, discard);
    wake_all(&pair->writers);
  } else {
    pair->writer_closed = true;
    wake_all(&pair->readers);
  }
}

bool pipe_grant_retain(struct pipe_end *end)
{
  struct pipe_pair *pair = end->pair;
  lock_pipe(pair);
  bool retained = end->grants != SIZE_MAX;
  if (retained) {
    ++end->grants;
  }
  unlock_pipe(pair);
  return retained;
}

void pipe_grant_release(struct pipe_end *end)
{
  struct pipe_pair *pair = end->pair;
  struct pipe_discard discard = {0};
  lock_pipe(pair);
  KASSERT(end->grants);
  bool closed = --end->grants == 0;
  if (closed) {
    close_end(end, &discard);
  }
  unlock_pipe(pair);
  release_discard(&discard);
  if (closed) {
    notify_pair(pair);
    readiness_pipe_notify();
  }
}

static void destroy_pipe_end(struct kernel_object *object)
{
  struct pipe_end *end = (struct pipe_end *)object;
  struct pipe_pair *pair = end->pair;
  struct pipe_discard discard = {0};
  lock_pipe(pair);
  KASSERT(!end->grants && pair->remaining_ends);
  if (end->reader) {
    KASSERT(!pair->readers);
  } else {
    KASSERT(!pair->writers);
  }
  /* Failed unpublished creation may never have installed any grant. */
  close_end(end, &discard);
  bool finished = --pair->remaining_ends == 0;
  KASSERT(!finished || (!pair->value_count && !pair->value_admitting && !pair->receiving));
  unlock_pipe(pair);
  release_discard(&discard);
  readiness_pipe_notify();
  if (finished) {
    kfree(pair);
  }
}

static bool pipe_pair_create(bool values, struct pipe_end **reader, struct pipe_end **writer)
{
  KASSERT(arch_cpu_index() == 0 && reader && writer && reader != writer);
  *reader = NULL;
  *writer = NULL;
  struct pipe_pair *pair = kmalloc(sizeof(*pair));
  if (!pair) {
    return false;
  }
  memset(pair, 0, sizeof(*pair));
  atomic_init(&pair->locked, false);
  pair->remaining_ends = PIPE_END_COUNT;
  pair->values_enabled = values;
  object_init(&pair->reader.object, OBJECT_PIPE, destroy_pipe_end);
  object_init(&pair->writer.object, OBJECT_PIPE, destroy_pipe_end);
  pair->reader.pair = pair;
  pair->reader.reader = true;
  pair->writer.pair = pair;
  *reader = &pair->reader;
  *writer = &pair->writer;
  return true;
}

static enum call_status pipe_install_status(enum capability_result result)
{
  if (result == CAP_NO_MEMORY) {
    return CALL_NO_MEMORY;
  }
  if (result == CAP_DENIED) {
    return CALL_DENIED;
  }
  KASSERT(result == CAP_LIMIT);
  return CALL_LIMIT;
}

void pipe_create_execute(struct pipe_create_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->reservation.table);
  struct pipe_end *reader, *writer;
  request->result = CALL_NO_MEMORY;
  if (pipe_pair_create(request->values, &reader, &writer)) {
    struct capability_grant grants[2] = {0};
    enum capability_result result = capability_grant_retain(&reader->object,
        PIPE_RIGHT_READ, 0, &grants[0]);
    if (result == CAP_OK) {
      result = capability_grant_retain(&writer->object, PIPE_RIGHT_WRITE, 0, &grants[1]);
      for (size_t i = 0; i < 2 && result == CAP_OK; ++i) {
        result = forwarder_derive_grant(&grants[i], task_forwarder_operation());
      }
      if (result == CAP_OK && !task_operation_start()) {
        result = CAP_DENIED;
      }
      if (result == CAP_OK) {
        KASSERT(capability_validate_grants(request->reservation.table, grants, 2) == CAP_OK);
        handle_t handles[2];
        capability_install_reserved(&request->reservation, request->slots, grants, 2, handles);
        request->reply = (struct pipe_create_reply){handles[0], handles[1]};
        request->result = CALL_OK;
      } else {
        request->result = pipe_install_status(result);
      }
    } else {
      request->result = pipe_install_status(result);
    }
    for (size_t i = 0; i < 2; ++i) {
      capability_grant_release(&grants[i]);
    }
    object_release(&reader->object);
    object_release(&writer->object);
  }
  capability_reservation_release(&request->reservation, request->slots);
}

static enum call_status create_pipe(bool values, struct pipe_create_reply *reply)
{
  struct process *process = process_current();
  KASSERT(process);
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  enum capability_result reserved = capability_request_reservation(2, &reservation, slots);
  if (reserved != CAP_OK) {
    return pipe_install_status(reserved);
  }
  if (task_stop_requested() || task_operation_denied()) {
    capability_reservation_release(&reservation, slots);
    return CALL_ENDPOINT_CLOSED;
  }
  struct pipe_create_request *request =
      (struct pipe_create_request *)bsp_request_prepare(BSP_SERVICE_PIPE_CREATE);
  request->reservation = reservation;
  memcpy(request->slots, slots, sizeof(slots));
  reservation = (struct capability_reservation){0};
  request->reply = (struct pipe_create_reply){0};
  request->values = values;
  request->result = CALL_NO_MEMORY;

  bsp_request_submit_and_wait(&request->request);
  enum call_status result = request->result;
  if (result == CALL_OK) {
    *reply = request->reply;
  }
  bsp_request_release(&request->request);
  return result;
}

static void destroy_pipe_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *pipe_service_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *service = kmalloc(sizeof(*service));
  if (service) {
    object_init(service, OBJECT_PIPE_SERVICE, destroy_pipe_service);
  }
  return service;
}

struct syscall_result pipe_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != PIPE_CREATE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & PIPE_SERVICE_RIGHT_CREATE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct pipe_create_options request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if ((request_size && request_size != payload_size) ||
      reply_capacity < sizeof(struct pipe_create_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (request_size && !copy_from_user(&request.flags, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.flags & ~PIPE_CREATE_VALUES) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct pipe_create_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct pipe_create_reply reply;
  enum call_status status = create_pipe((request.flags & PIPE_CREATE_VALUES) != 0, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    handle_t handles[] = {reply.reader, reply.writer};
    capability_close_returned(&process_current()->capabilities, handles, 2);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static void copy_ring_out(struct pipe_pair *pair, uint8_t *destination, size_t length)
{
  size_t first = PIPE_CAPACITY - pair->head;
  if (first > length) {
    first = length;
  }
  memcpy(destination, pair->data + pair->head, first);
  memcpy(destination + first, pair->data, length - first);
  pair->head = (pair->head + length) % PIPE_CAPACITY;
  pair->count -= length;
}

static void copy_ring_in(struct pipe_pair *pair, const uint8_t *source, size_t length)
{
  size_t tail = (pair->head + pair->count) % PIPE_CAPACITY;
  size_t first = PIPE_CAPACITY - tail;
  if (first > length) {
    first = length;
  }
  memcpy(pair->data + tail, source, first);
  memcpy(pair->data, source + first, length - first);
  pair->count += length;
}

static enum call_status read_pipe(struct pipe_pair *pair, uint8_t *data,
    size_t capacity, size_t *read, bool try)
{
  *read = 0;
  if (!capacity) {
    return CALL_OK;
  }
  for (;;) {
    lock_pipe(pair);
    if (task_stop_requested() || task_operation_denied()) {
      unlock_pipe(pair);
      return CALL_ENDPOINT_CLOSED;
    }
    if (value_at_head(pair)) {
      unlock_pipe(pair);
      return CALL_PIPE_VALUE;
    }
    if (pair->count || (pair->writer_closed && !pair->value_count)) {
      /* Bytes before the next value are already buffered; stop at it. */
      size_t available = pair->count;
      if (pair->value_count) {
        uint64_t before = pair->values[pair->value_head].position - consumed(pair);
        available = before < available ? (size_t)before : available;
      }
      size_t length = available < capacity ? available : capacity;
      bool writable = pair->count == PIPE_CAPACITY && length;
      if (!task_operation_start()) {
        unlock_pipe(pair);
        return CALL_DENIED;
      }
      copy_ring_out(pair, data, length);
      if (length) {
        wake_all(&pair->writers);
      }
      *read = length;
      unlock_pipe(pair);
      if (length) {
        notify_pair(pair);
      }
      if (writable) {
        readiness_pipe_notify();
      }
      return CALL_OK;
    }
    if (try) {
      unlock_pipe(pair);
      return CALL_WOULD_BLOCK;
    }
    struct task_wait_link *record = task_wait_link_prepare();
    record->next = pair->readers;
    pair->readers = record;
    struct task_wait *wait = record->wait;
    unlock_pipe(pair);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_pipe(pair);
    detach_waiter(&pair->readers, record);
    bool stopped = !resumed || (task_stop_requested() || task_operation_denied());
    unlock_pipe(pair);
    if (stopped) {
      return CALL_ENDPOINT_CLOSED;
    }
  }
}

static enum call_status write_pipe(struct pipe_pair *pair, const uint8_t *data,
    size_t length, size_t *written, bool try)
{
  *written = 0;
  if (!length) {
    return CALL_OK;
  }
  for (;;) {
    lock_pipe(pair);
    if ((task_stop_requested() || task_operation_denied()) || pair->reader_closed) {
      unlock_pipe(pair);
      return CALL_ENDPOINT_CLOSED;
    }
    if (pair->count < PIPE_CAPACITY) {
      size_t available = PIPE_CAPACITY - pair->count;
      bool readable = !pair->count;
      *written = length < available ? length : available;
      if (!task_operation_start()) {
        *written = 0;
        unlock_pipe(pair);
        return CALL_DENIED;
      }
      copy_ring_in(pair, data, *written);
      pair->written += *written;
      wake_all(&pair->readers);
      unlock_pipe(pair);
      notify_pair(pair);
      if (readable) {
        readiness_pipe_notify();
      }
      return CALL_OK;
    }
    if (try) {
      unlock_pipe(pair);
      return CALL_WOULD_BLOCK;
    }
    struct task_wait_link *record = task_wait_link_prepare();
    record->next = pair->writers;
    pair->writers = record;
    struct task_wait *wait = record->wait;
    unlock_pipe(pair);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_pipe(pair);
    detach_waiter(&pair->writers, record);
    bool stopped = !resumed || (task_stop_requested() || task_operation_denied());
    unlock_pipe(pair);
    if (stopped) {
      return CALL_ENDPOINT_CLOSED;
    }
  }
}

static struct syscall_result pipe_read_call(struct pipe_pair *pair,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, bool try)
{
  struct pipe_read_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct pipe_read_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.buffer, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t capacity = request.capacity < PIPE_READ_MAX_BYTES ? request.capacity : PIPE_READ_MAX_BYTES;
  if (!user_buffer_check(request.buffer, capacity, USER_BUFFER_WRITE) ||
      !user_buffer_check(reply_address, sizeof(struct pipe_read_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (capacity && request.buffer < reply_address + sizeof(struct pipe_read_reply) &&
      reply_address < request.buffer + capacity) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  uint8_t data[PIPE_READ_MAX_BYTES];
  struct pipe_read_reply reply;
  enum call_status status = read_pipe(pair, data, capacity, &reply.length, try);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_reply_to_user(reply_address, &reply, sizeof(reply),
      request.buffer, data, reply.length)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result pipe_write_call(struct pipe_pair *pair,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, bool try)
{
  struct pipe_write_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct pipe_write_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.buffer, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct pipe_write_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t length = request.length < PIPE_WRITE_MAX_BYTES ? request.length : PIPE_WRITE_MAX_BYTES;
  uint8_t data[PIPE_WRITE_MAX_BYTES];
  if (!copy_from_user(data, request.buffer, length)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  size_t written;
  enum call_status status = write_pipe(pair, data, length, &written, try);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct pipe_write_reply reply = {.length = written};
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static enum call_status cap_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT:
  case CAP_FULL: return CALL_LIMIT;
  default: return CALL_BAD_REQUEST;
  }
}

/* Waits on the writers queue for a free value slot. Called and returns with
 * the pipe lock held when it returns CALL_OK. */
static enum call_status admit_value(struct pipe_pair *pair, bool try)
{
  for (;;) {
    lock_pipe(pair);
    if ((task_stop_requested() || task_operation_denied()) || pair->reader_closed) {
      unlock_pipe(pair);
      return CALL_ENDPOINT_CLOSED;
    }
    if (pair->value_count + pair->value_admitting < PIPE_VALUE_SLOTS) {
      ++pair->value_admitting;
      return CALL_OK;
    }
    if (try) {
      unlock_pipe(pair);
      return CALL_WOULD_BLOCK;
    }
    struct task_wait_link *record = task_wait_link_prepare();
    record->next = pair->writers;
    pair->writers = record;
    struct task_wait *wait = record->wait;
    unlock_pipe(pair);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_pipe(pair);
    detach_waiter(&pair->writers, record);
    bool stopped = !resumed || (task_stop_requested() || task_operation_denied());
    unlock_pipe(pair);
    if (stopped) {
      return CALL_ENDPOINT_CLOSED;
    }
  }
}

/* Moves the caller's handle into the pipe as a READ-only grant on the same
 * object. Every rejection before the move leaves the handle with the caller. */
static struct syscall_result pipe_handoff_call(struct pipe_pair *pair,
    uintptr_t request_address, size_t request_size, bool try)
{
  struct pipe_handoff_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!pair->values_enabled) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!copy_from_user(&request.value, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_table *table = &process_current()->capabilities;
  struct capability_reference reference = {0};
  enum capability_result result = capability_acquire_metadata(table, request.value, &reference);
  if (result != CAP_OK) {
    return (struct syscall_result){cap_status(result), 0};
  }
  enum call_status status = CALL_OK;
  if (object_effective_type(reference.object) == OBJECT_OUTPUT_PRODUCER ||
      !object_stream_valid(reference.object, PROTOCOL_FILE, reference.transport)) {
    status = CALL_WRONG_TYPE;
  } else if (!(reference.rights & FILE_RIGHT_READ)) {
    status = CALL_DENIED;
  }
  /* Retain the narrowed grant first, so taking the source cannot be the
   * object's last grant and the move has no logical close effect. A value's
   * receiver may read and size it but never learns its free-text detail. */
  struct capability_grant narrowed = {0};
  if (status == CALL_OK) {
    status = cap_status(capability_grant_derive(reference.object, FILE_RIGHT_READ,
        reference.transport, reference.restrictions | HANDLE_RESTRICT_DETAIL, &narrowed));
  }
  if (status == CALL_OK) {
    status = admit_value(pair, try);
  }
  if (status != CALL_OK) {
    capability_grant_release(&narrowed);
    capability_release(&reference);
    return (struct syscall_result){status, 0};
  }
  bool started = task_operation_start();
  unlock_pipe(pair);
  notify_pair(pair);
  struct capability_grant moved = {0};
  status = started ? cap_status(capability_grant_take(table, &request.value, &reference, 1, &moved)) :
      CALL_DENIED;

  bool readable = false;
  lock_pipe(pair);
  KASSERT(pair->value_admitting);
  --pair->value_admitting;
  if (status == CALL_OK && !pair->reader_closed) {
    /* Accepted ownership after the reader left is dropped, like bytes. */
    size_t tail = (pair->value_head + pair->value_count) % PIPE_VALUE_SLOTS;
    pair->values[tail] = (struct pipe_value){narrowed, pair->written};
    narrowed = (struct capability_grant){0};
    readable = ++pair->value_count == 1 && !pair->count;
    wake_all(&pair->readers);
  } else if (status != CALL_OK) {
    wake_all(&pair->writers);
  }
  unlock_pipe(pair);
  notify_pair(pair);
  capability_grant_release(&narrowed);
  capability_grant_release(&moved);
  capability_release(&reference);
  if (readable) {
    readiness_pipe_notify();
  }
  return (struct syscall_result){status, 0};
}

/* Reports the head segment, installing a head value. The receiving flag keeps
 * that value at the head while the destination slot is reserved and the grant
 * validated outside the pipe lock; any failure leaves it queued. */
static enum call_status receive_head(struct pipe_pair *pair, bool try,
    handle_t *value, uint64_t *head)
{
  for (;;) {
    lock_pipe(pair);
    if (task_stop_requested() || task_operation_denied()) {
      unlock_pipe(pair);
      return CALL_ENDPOINT_CLOSED;
    }
    if (!pair->receiving && value_at_head(pair)) {
      pair->receiving = true;
      struct capability_grant grant = pair->values[pair->value_head].grant;
      unlock_pipe(pair);
      notify_pair(pair);
      struct capability_reservation reservation;
      struct capability_reserved_slot slot;
      enum call_status status = cap_status(capability_request_reservation(1, &reservation, &slot));
      if (status == CALL_OK) {
        status = cap_status(capability_validate_grants(reservation.table, &grant, 1));
        if (status == CALL_OK && !task_operation_start()) {
          status = CALL_DENIED;
        }
        if (status != CALL_OK) {
          capability_reservation_release(&reservation, &slot);
        }
      }
      struct pipe_discard discard = {0};
      lock_pipe(pair);
      pair->receiving = false;
      if (status == CALL_OK) {
        /* Last-reader closure keeps a value under receipt at the head. */
        KASSERT(pair->value_count && pair->values[pair->value_head].grant.object == grant.object);
        pair->values[pair->value_head] = (struct pipe_value){0};
        pair->value_head = (pair->value_head + 1) % PIPE_VALUE_SLOTS;
        --pair->value_count;
        wake_all(&pair->writers);
      }
      if (pair->reader_closed) {
        drop_values(pair, &discard);
      }
      wake_all(&pair->readers);
      unlock_pipe(pair);
      notify_pair(pair);
      release_discard(&discard);
      if (status == CALL_OK) {
        capability_install_reserved(&reservation, &slot, &grant, 1, value);
        *head = PIPE_HEAD_VALUE;
      }
      return status;
    }
    if (!pair->receiving && pair->count) {
      unlock_pipe(pair);
      *head = PIPE_HEAD_BYTES;
      return CALL_OK;
    }
    if (!pair->receiving && pair->writer_closed && !pair->value_count) {
      unlock_pipe(pair);
      *head = PIPE_HEAD_END;
      return CALL_OK;
    }
    if (try) {
      unlock_pipe(pair);
      return CALL_WOULD_BLOCK;
    }
    struct task_wait_link *record = task_wait_link_prepare();
    record->next = pair->readers;
    pair->readers = record;
    struct task_wait *wait = record->wait;
    unlock_pipe(pair);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_pipe(pair);
    detach_waiter(&pair->readers, record);
    bool stopped = !resumed || (task_stop_requested() || task_operation_denied());
    unlock_pipe(pair);
    if (stopped) {
      return CALL_ENDPOINT_CLOSED;
    }
  }
}

static struct syscall_result pipe_receive_call(struct pipe_pair *pair, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, bool try)
{
  if (request_size || reply_capacity < sizeof(struct pipe_receive_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct pipe_receive_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct pipe_receive_reply reply = {0};
  enum call_status status = receive_head(pair, try, &reply.value, &reply.head);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (reply.head == PIPE_HEAD_VALUE) {
    readiness_pipe_notify();
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    if (reply.value) {
      capability_close_returned(&process_current()->capabilities, &reply.value, 1);
    }
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result pipe_call(struct pipe_end *end, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == PIPE_READ || operation == PIPE_TRY_READ) {
    if (!(rights & PIPE_RIGHT_READ)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (!end->reader) {
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    return pipe_read_call(end->pair, request_address, request_size,
        reply_address, reply_capacity, operation == PIPE_TRY_READ);
  }
  if (operation == PIPE_WRITE || operation == PIPE_TRY_WRITE) {
    if (!(rights & PIPE_RIGHT_WRITE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (end->reader) {
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    return pipe_write_call(end->pair, request_address, request_size,
        reply_address, reply_capacity, operation == PIPE_TRY_WRITE);
  }
  if (operation == PIPE_HANDOFF || operation == PIPE_TRY_HANDOFF) {
    if (!(rights & PIPE_RIGHT_WRITE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (end->reader) {
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    return pipe_handoff_call(end->pair, request_address, request_size,
        operation == PIPE_TRY_HANDOFF);
  }
  if (operation == PIPE_RECEIVE || operation == PIPE_TRY_RECEIVE) {
    if (!(rights & PIPE_RIGHT_READ)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (!end->reader) {
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    return pipe_receive_call(end->pair, request_size, reply_address, reply_capacity,
        operation == PIPE_TRY_RECEIVE);
  }
  return (struct syscall_result){CALL_BAD_OPERATION, 0};
}

uint64_t pipe_ready(struct pipe_end *end, uint64_t events)
{
  uint64_t flags = cpu_save_interrupts();
  struct pipe_pair *pair = end->pair;
  lock_pipe(pair);
  uint64_t ready = 0;
  if (end->reader) {
    if ((events & WAIT_READABLE) && (pair->count || pair->value_count)) {
      ready |= WAIT_READABLE;
    }
    if ((events & (WAIT_READABLE | WAIT_PEER_FIN | WAIT_READ_STATUS)) && pair->writer_closed) {
      ready |= WAIT_PEER_FIN;
    }
  } else {
    if ((events & WAIT_WRITABLE) && !pair->reader_closed && pair->count < PIPE_CAPACITY) {
      ready |= WAIT_WRITABLE;
    }
    if ((events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED | WAIT_WRITE_STATUS)) && pair->reader_closed) {
      ready |= WAIT_WRITE_CLOSED;
    }
  }
  unlock_pipe(pair);
  cpu_restore_interrupts(flags);
  return ready;
}
