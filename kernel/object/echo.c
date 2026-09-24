#include <abi/echo.h>
#include <kernel/mm/heap.h>
#include <kernel/net/echo.h>
#include <kernel/object/echo.h>
#include <kernel/panic.h>
#include <kernel/user_memory.h>

static void destroy_echo(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *echo_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_ECHO, destroy_echo);
  }
  return object;
}

struct syscall_result echo_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != ECHO_EXCHANGE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & ECHO_RIGHT_SEND)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct echo_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct echo_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.destination, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct echo_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.reserved) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct echo_reply reply;
  enum call_status status = net_echo_exchange(request.destination, request.deadline_ns, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The sole user task kept its mappings and capability alive while blocked. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
