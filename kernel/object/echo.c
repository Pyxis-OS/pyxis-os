#include <abi/echo.h>
#include <kernel/net/factory.h>
#include <kernel/object/net_factory.h>
#include <kernel/net/echo.h>
#include <kernel/object/echo.h>
#include <kernel/panic.h>
#include <kernel/user_memory.h>

struct syscall_result echo_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == ECHO_NEXT_INTERFACE) {
    return net_factory_next_interface_call((struct net_factory *)object, rights,
        request_address, request_size, reply_address, reply_capacity);
  }
  if (operation == ECHO_DERIVE) {
    return net_factory_derive_call((struct net_factory *)object, rights,
        request_address, request_size, reply_address, reply_capacity);
  }

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
  if (!copy_from_user(&request.path, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct echo_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.path.reserved || request.reserved) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct echo_reply reply;
  enum call_status status = net_echo_exchange((struct net_factory *)object,
      request.path, request.destination, request.deadline_ns, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The admitted reference keeps storage alive; revalidate output after the
   * wait because a sibling may have withdrawn its mapping. */
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
