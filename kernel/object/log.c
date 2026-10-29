#include <abi/console.h>
#include <abi/log.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/log_ring.h>
#include <kernel/mm/heap.h>
#include <kernel/object/capability.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/log.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

/* Written text is one line: control bytes other than tab are logged as '?'. */
static char sanitised(unsigned char c)
{
  return (c < ' ' && c != '\t') || c == 0x7f ? '?' : (char)c;
}

/* A write-only console-protocol stream. Writes are assembled into lines of at
 * most LOG_WRITE_MAX bytes; the line lock serializes writers of one stream. */
struct log_stream {
  struct kernel_object object;
  struct space *space;
  enum log_level level;
  atomic_bool locked;
  size_t length;
  char line[LOG_WRITE_MAX];
};

#define LOG_STREAM_WRITE_MAX 1024

static void destroy_log_stream(struct kernel_object *object)
{
  struct log_stream *stream = (struct log_stream *)object;
  if (stream->length) {
    log_write_line(stream->space, stream->level, stream->line, stream->length);
  }
  kfree(stream);
}

void log_stream_create_execute(struct log_stream_create_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  request->created = NULL;
  struct log_stream *stream = kmalloc(sizeof(*stream));
  if (!stream) {
    request->status = CALL_NO_MEMORY;
    return;
  }
  *stream = (struct log_stream){.space = request->space, .level = (enum log_level)request->level};
  atomic_init(&stream->locked, false);
  object_init(&stream->object, OBJECT_LOG_STREAM, destroy_log_stream);
  request->created = &stream->object;
  request->status = CALL_OK;
}

static void stream_lock(struct log_stream *stream)
{
  while (atomic_exchange_explicit(&stream->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void stream_flush(struct log_stream *stream)
{
  if (stream->length) {
    log_write_line(stream->space, stream->level, stream->line, stream->length);
    stream->length = 0;
  }
}

static void stream_put(struct log_stream *stream, unsigned char c)
{
  if (c == '\n') {
    stream_flush(stream);
    return;
  }
  if (c == '\r') {
    return;
  }
  if (stream->length == sizeof(stream->line)) {
    stream_flush(stream);
  }
  stream->line[stream->length++] = sanitised(c);
}

struct syscall_result log_stream_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct log_stream *stream = (struct log_stream *)object;
  if (operation != CONSOLE_WRITE && operation != CONSOLE_TRY_WRITE &&
      operation != CONSOLE_FRESH_LINE && operation != CONSOLE_SET_TAB_WIDTH &&
      operation != CONSOLE_DRAIN) {
    return (struct syscall_result){operation == CONSOLE_READ || operation == CONSOLE_SIZE ?
        CALL_UNAVAILABLE : CALL_BAD_OPERATION, 0};
  }
  if (!(rights & CONSOLE_RIGHT_WRITE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  union console_payload request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation != CONSOLE_WRITE && operation != CONSOLE_TRY_WRITE) {
    if (operation == CONSOLE_FRESH_LINE) {
      uint64_t flags = cpu_save_interrupts();
      stream_lock(stream);
      stream_flush(stream);
      atomic_store_explicit(&stream->locked, false, memory_order_release);
      cpu_restore_interrupts(flags);
    }
    return (struct syscall_result){task_operation_start() ? CALL_OK : CALL_DENIED, 0};
  }
  struct console_write_reply reply;
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  size_t count = request.write.length < LOG_STREAM_WRITE_MAX ?
      request.write.length : LOG_STREAM_WRITE_MAX;
  unsigned char bytes[LOG_STREAM_WRITE_MAX];
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      !copy_from_user(bytes, request.write.address, count)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!task_operation_start()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  uint64_t flags = cpu_save_interrupts();
  stream_lock(stream);
  for (size_t i = 0; i < count; ++i) {
    stream_put(stream, bytes[i]);
  }
  atomic_store_explicit(&stream->locked, false, memory_order_release);
  cpu_restore_interrupts(flags);
  reply.written = count;
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static void destroy_log(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *log_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_LOG, destroy_log);
  }
  return object;
}

static enum call_status capability_call_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK:
    return CALL_OK;
  case CAP_NO_MEMORY:
    return CALL_NO_MEMORY;
  case CAP_DENIED:
    return CALL_DENIED;
  default:
    return CALL_LIMIT;
  }
}

static struct syscall_result log_write_call(uint64_t rights, uintptr_t request_address,
    size_t request_size)
{
  struct log_write_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (!(rights & LOG_RIGHT_WRITE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size != payload_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.level, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.level < LOG_LEVEL_ERROR || request.level > LOG_LEVEL_TRACE) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  size_t length = request.length < LOG_WRITE_MAX ? request.length : LOG_WRITE_MAX;
  char text[LOG_WRITE_MAX];
  if (!copy_from_user(text, request.address, length)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!task_operation_start()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  for (size_t i = 0; i < length; ++i) {
    text[i] = sanitised((unsigned char)text[i]);
  }
  log_write_line(process_current()->space, (enum log_level)request.level, text, length);
  return (struct syscall_result){CALL_OK, 0};
}

static struct syscall_result log_stream_open(uint64_t rights, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  struct log_stream_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (!(rights & LOG_RIGHT_WRITE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size != payload_size || reply_capacity < sizeof(struct log_stream_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.level, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.level < LOG_LEVEL_ERROR || request.level > LOG_LEVEL_TRACE ||
      !user_buffer_check(reply_address, sizeof(struct log_stream_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  enum capability_result acquired = capability_request_reservation(1, &reservation, &slot);
  if (acquired != CAP_OK) {
    return (struct syscall_result){capability_call_status(acquired), 0};
  }
  struct process *caller = process_current();
  struct log_stream_create_request *bsp =
      (void *)bsp_request_prepare(BSP_SERVICE_LOG_STREAM_CREATE);
  bsp->space = caller->space;
  bsp->level = request.level;
  bsp_request_submit_and_wait(&bsp->request);
  enum call_status status = bsp->status;
  struct kernel_object *created = bsp->created;
  bsp->created = NULL;
  bsp_request_release(&bsp->request);

  struct log_stream_reply reply = {0};
  if (status == CALL_OK) {
    struct capability_grant grant = {0};
    acquired = capability_grant_retain(created, CONSOLE_RIGHT_WRITE, 0, &grant);
    if (acquired == CAP_OK) {
      acquired = forwarder_request_derivation(&grant);
    }
    if (acquired == CAP_OK) {
      acquired = capability_validate_grants(reservation.table, &grant, 1);
    }
    if (acquired == CAP_OK && !task_operation_start()) {
      acquired = CAP_DENIED;
    }
    status = capability_call_status(acquired);
    if (status == CALL_OK) {
      capability_install_reserved(&reservation, &slot, &grant, 1, &reply.handle);
    }
    capability_grant_release(&grant);
  }
  capability_reservation_release(&reservation, &slot);
  if (status == CALL_OK &&
      !copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1)) {
    capability_close_returned(&caller->capabilities, &reply.handle, 1);
    status = CALL_BAD_BUFFER;
  }
  if (created) {
    object_release(created);
  }
  return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
}

struct syscall_result log_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == LOG_WRITE) {
    return log_write_call(rights, request_address, request_size);
  }
  if (operation == LOG_STREAM) {
    return log_stream_open(rights, request_address, request_size, reply_address,
        reply_capacity);
  }
  if (operation != LOG_SNAPSHOT && operation != LOG_READ) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & LOG_RIGHT_READ)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == LOG_SNAPSHOT) {
    struct log_snapshot snapshot;
    if (request_size || reply_capacity < sizeof(snapshot)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(snapshot), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (!log_ring_snapshot(&snapshot)) {
      return (struct syscall_result){CALL_UNAVAILABLE, 0};
    }
    if (!copy_to_user(reply_address, &snapshot, sizeof(snapshot))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(snapshot)};
  }
  struct {
    struct log_cursor cursor;
    struct log_cursor end;
  } request;
  struct {
    struct log_read_reply header;
    char records[LOG_READ_MAX];
  } reply;
  if (request_size != sizeof(request) ||
      reply_capacity <= sizeof(reply.header) + sizeof(struct log_record)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t capacity = reply_capacity - sizeof(reply.header);
  if (capacity > sizeof(reply.records)) {
    capacity = sizeof(reply.records);
  }
  if (!user_buffer_check(reply_address, sizeof(reply.header) + capacity, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  enum call_status status = log_ring_read(request.cursor, request.end,
      &reply.header, reply.records, capacity);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  size_t bytes = sizeof(reply.header) + reply.header.size;
  if (!copy_to_user(reply_address, &reply, bytes)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, bytes};
}
