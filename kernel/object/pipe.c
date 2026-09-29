#include <abi/pipe.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/pipe.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

struct pipe_pair {
  atomic_bool locked;
  struct pipe_end reader, writer;
  struct pipe_wait *readers, *writers;
  size_t head, count;
  bool reader_closed, writer_closed;
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
static void wake_all(struct pipe_wait **queue)
{
  struct pipe_wait *record = *queue;
  *queue = NULL;
  while (record) {
    struct pipe_wait *next = record->next;
    struct task_wait *wait = record->wait;
    record->next = NULL;
    task_wait_wake(wait);
    record = next;
  }
}

static void destroy_pipe_end(struct kernel_object *object)
{
  struct pipe_end *end = (struct pipe_end *)object;
  struct pipe_pair *pair = end->pair;
  lock_pipe(pair);
  /* A blocked operation keeps its own endpoint live through its process handle. */
  if (end->reader) {
    KASSERT(!pair->reader_closed && !pair->readers);
    pair->reader_closed = true;
    wake_all(&pair->writers);
  } else {
    KASSERT(!pair->writer_closed && !pair->writers);
    pair->writer_closed = true;
    wake_all(&pair->readers);
  }
  bool finished = pair->reader_closed && pair->writer_closed;
  unlock_pipe(pair);
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
  KASSERT(arch_cpu_index() == 0 && request->table);
  struct pipe_end *reader, *writer;
  request->result = CALL_NO_MEMORY;
  if (pipe_pair_create(&reader, &writer)) {
    handle_t read_handle, write_handle;
    enum capability_result result = capability_install(request->table,
        &reader->object, PIPE_RIGHT_READ, 0, &read_handle);
    if (result == CAP_OK) {
      result = capability_install(request->table,
          &writer->object, PIPE_RIGHT_WRITE, 0, &write_handle);
      if (result == CAP_OK) {
        request->reply = (struct pipe_create_reply){read_handle, write_handle};
        request->result = CALL_OK;
      } else {
        KASSERT(capability_close(request->table, read_handle) == CAP_OK);
        request->result = pipe_install_status(result);
      }
    } else {
      request->result = pipe_install_status(result);
    }
    object_release(&reader->object);
    object_release(&writer->object);
  }
  request->table = NULL;
}

static enum call_status create_pipe(struct pipe_create_reply *reply)
{
  struct process *process = process_current();
  KASSERT(process);
  struct pipe_create_request *request =
      (struct pipe_create_request *)bsp_request_prepare(BSP_SERVICE_PIPE_CREATE);
  request->table = &process->capabilities;
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

static size_t read_pipe(struct pipe_pair *pair, uint8_t *data, size_t capacity)
{
  if (!capacity) {
    return 0;
  }
  for (;;) {
    struct pipe_wait *record = task_prepare_pipe_wait();
    lock_pipe(pair);
    if (pair->count || pair->writer_closed) {
      size_t length = pair->count < capacity ? pair->count : capacity;
      copy_ring_out(pair, data, length);
      if (length) {
        wake_all(&pair->writers);
      }
      unlock_pipe(pair);
      return length;
    }
    record->next = pair->readers;
    pair->readers = record;
    unlock_pipe(pair);
    task_wait_sleep(record->wait);
    /* Waker detached this task-owned record before resumption. */
  }
}

static enum call_status write_pipe(struct pipe_pair *pair, const uint8_t *data,
    size_t length, size_t *written)
{
  *written = 0;
  if (!length) {
    return CALL_OK;
  }
  for (;;) {
    struct pipe_wait *record = task_prepare_pipe_wait();
    lock_pipe(pair);
    if (pair->reader_closed) {
      unlock_pipe(pair);
      return CALL_ENDPOINT_CLOSED;
    }
    if (pair->count < PIPE_CAPACITY) {
      size_t available = PIPE_CAPACITY - pair->count;
      *written = length < available ? length : available;
      copy_ring_in(pair, data, *written);
      wake_all(&pair->readers);
      unlock_pipe(pair);
      return CALL_OK;
    }
    record->next = pair->writers;
    pair->writers = record;
    unlock_pipe(pair);
    task_wait_sleep(record->wait);
  }
}

static struct syscall_result pipe_read_call(struct pipe_pair *pair,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
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
  struct pipe_read_reply reply = {.length = read_pipe(pair, data, capacity)};
  KASSERT(copy_to_user(request.buffer, data, reply.length));
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result pipe_write_call(struct pipe_pair *pair,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
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
  enum call_status status = write_pipe(pair, data, length, &written);
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
  if (operation == PIPE_READ) {
    if (!(rights & PIPE_RIGHT_READ)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (!end->reader) {
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    return pipe_read_call(end->pair, request_address, request_size,
        reply_address, reply_capacity);
  }
  if (operation == PIPE_WRITE) {
    if (!(rights & PIPE_RIGHT_WRITE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (end->reader) {
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    return pipe_write_call(end->pair, request_address, request_size,
        reply_address, reply_capacity);
  }
  return (struct syscall_result){CALL_BAD_OPERATION, 0};
}
