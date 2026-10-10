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

struct pipe_pair {
  atomic_bool locked;
  struct pipe_end reader, writer;
  struct task_wait_link *readers, *writers;
  size_t head, count;
  bool reader_closed, writer_closed;
  size_t remaining_ends;
  uint8_t data[PIPE_CAPACITY];
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

/* Storage references keep the pair alive without keeping a direction open. */
static void close_end(struct pipe_end *end)
{
  struct pipe_pair *pair = end->pair;
  if (end->reader) {
    pair->reader_closed = true;
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
  lock_pipe(pair);
  KASSERT(end->grants);
  bool closed = --end->grants == 0;
  if (closed) {
    close_end(end);
  }
  unlock_pipe(pair);
  if (closed) {
    readiness_pipe_notify();
  }
}

static void destroy_pipe_end(struct kernel_object *object)
{
  struct pipe_end *end = (struct pipe_end *)object;
  struct pipe_pair *pair = end->pair;
  lock_pipe(pair);
  KASSERT(!end->grants && pair->remaining_ends);
  if (end->reader) {
    KASSERT(!pair->readers);
  } else {
    KASSERT(!pair->writers);
  }
  /* Failed unpublished creation may never have installed any grant. */
  close_end(end);
  bool finished = --pair->remaining_ends == 0;
  unlock_pipe(pair);
  readiness_pipe_notify();
  if (finished) {
    kfree(pair);
  }
}

static bool pipe_pair_create(struct pipe_end **reader, struct pipe_end **writer)
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
  KASSERT(result == CAP_LIMIT);
  return CALL_LIMIT;
}

void pipe_create_execute(struct pipe_create_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->reservation.table);
  struct pipe_end *reader, *writer;
  request->result = CALL_NO_MEMORY;
  if (pipe_pair_create(&reader, &writer)) {
    struct capability_grant grants[2] = {0};
    enum capability_result result = capability_grant_retain(&reader->object,
        PIPE_RIGHT_READ, 0, &grants[0]);
    if (result == CAP_OK) {
      result = capability_grant_retain(&writer->object, PIPE_RIGHT_WRITE, 0, &grants[1]);
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

static enum call_status create_pipe(struct pipe_create_reply *reply)
{
  struct process *process = process_current();
  KASSERT(process);
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  enum capability_result reserved = capability_request_reservation(2, &reservation, slots);
  if (reserved != CAP_OK) {
    return pipe_install_status(reserved);
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    return CALL_ENDPOINT_CLOSED;
  }
  struct pipe_create_request *request =
      (struct pipe_create_request *)bsp_request_prepare(BSP_SERVICE_PIPE_CREATE);
  request->reservation = reservation;
  memcpy(request->slots, slots, sizeof(slots));
  reservation = (struct capability_reservation){0};
  request->reply = (struct pipe_create_reply){0};
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
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != PIPE_CREATE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & PIPE_SERVICE_RIGHT_CREATE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size || reply_capacity < sizeof(struct pipe_create_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct pipe_create_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct pipe_create_reply reply;
  enum call_status status = create_pipe(&reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
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
    if (task_stop_requested()) {
      unlock_pipe(pair);
      return CALL_ENDPOINT_CLOSED;
    }
    if (pair->count || pair->writer_closed) {
      size_t length = pair->count < capacity ? pair->count : capacity;
      bool writable = pair->count == PIPE_CAPACITY && length;
      copy_ring_out(pair, data, length);
      if (length) {
        wake_all(&pair->writers);
      }
      *read = length;
      unlock_pipe(pair);
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
    bool stopped = !resumed || task_stop_requested();
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
    if (task_stop_requested() || pair->reader_closed) {
      unlock_pipe(pair);
      return CALL_ENDPOINT_CLOSED;
    }
    if (pair->count < PIPE_CAPACITY) {
      size_t available = PIPE_CAPACITY - pair->count;
      bool readable = !pair->count;
      *written = length < available ? length : available;
      copy_ring_in(pair, data, *written);
      wake_all(&pair->readers);
      unlock_pipe(pair);
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
    bool stopped = !resumed || task_stop_requested();
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
  KASSERT(copy_to_user(request.buffer, data, reply.length));
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
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
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
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
  return (struct syscall_result){CALL_BAD_OPERATION, 0};
}

uint64_t pipe_ready(struct pipe_end *end, uint64_t events)
{
  uint64_t flags = cpu_save_interrupts();
  struct pipe_pair *pair = end->pair;
  lock_pipe(pair);
  uint64_t ready = 0;
  if (end->reader) {
    if ((events & WAIT_READABLE) && pair->count) {
      ready |= WAIT_READABLE;
    }
    if ((events & (WAIT_READABLE | WAIT_PEER_FIN)) && pair->writer_closed) {
      ready |= WAIT_PEER_FIN;
    }
  } else {
    if ((events & WAIT_WRITABLE) && !pair->reader_closed && pair->count < PIPE_CAPACITY) {
      ready |= WAIT_WRITABLE;
    }
    if ((events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED)) && pair->reader_closed) {
      ready |= WAIT_WRITE_CLOSED;
    }
  }
  unlock_pipe(pair);
  cpu_restore_interrupts(flags);
  return ready;
}
