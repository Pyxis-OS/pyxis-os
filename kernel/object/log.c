#include <abi/log.h>
#include <arch/smp.h>
#include <kernel/log_ring.h>
#include <kernel/mm/heap.h>
#include <kernel/object/log.h>
#include <kernel/panic.h>
#include <kernel/user_memory.h>

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

struct syscall_result log_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
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
    KASSERT(copy_to_user(reply_address, &snapshot, sizeof(snapshot)));
    return (struct syscall_result){CALL_OK, sizeof(snapshot)};
  }
  struct {
    struct log_cursor cursor;
    struct log_cursor end;
  } request;
  struct {
    struct log_read_reply header;
    char text[LOG_READ_MAX];
  } reply;
  if (request_size != sizeof(request) || reply_capacity <= sizeof(reply.header)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t capacity = reply_capacity - sizeof(reply.header);
  if (capacity > sizeof(reply.text)) {
    capacity = sizeof(reply.text);
  }
  if (!user_buffer_check(reply_address, sizeof(reply.header) + capacity, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  enum call_status status = log_ring_read(request.cursor, request.end,
      &reply.header, reply.text, capacity);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  size_t bytes = sizeof(reply.header) + reply.header.size;
  KASSERT(copy_to_user(reply_address, &reply, bytes));
  return (struct syscall_result){CALL_OK, bytes};
}
