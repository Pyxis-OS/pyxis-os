#include <abi/tcp.h>
#include <kernel/mm/heap.h>
#include <kernel/net/tcp.h>
#include <kernel/object/tcp.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

static void destroy_tcp_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *tcp_service_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_TCP_SERVICE, destroy_tcp_service);
  }
  return object;
}

struct syscall_result tcp_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != TCP_CONNECT) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & TCP_SERVICE_RIGHT_CONNECT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct tcp_connect_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct tcp_connect_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.address, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct tcp_connect_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.reserved || !request.port) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct tcp_connect_reply reply;
  enum call_status status = net_tcp_connect(&process_current()->capabilities,
      request.address, request.port, request.deadline_ns, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The sole task keeps its checked mappings and table alive while parked. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result tcp_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case TCP_INSPECT: required = TCP_RIGHT_INSPECT; break;
  case TCP_ABORT: required = TCP_RIGHT_ABORT; break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  size_t reply_size = operation == TCP_INSPECT ? sizeof(struct tcp_connection_info) : 0;
  if (request_size || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct tcp_connection_info reply;
  enum call_status status = operation == TCP_INSPECT ?
      net_tcp_inspect(object, &reply) : net_tcp_abort(object);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (reply_size) {
    KASSERT(copy_to_user(reply_address, &reply, reply_size));
  }
  return (struct syscall_result){CALL_OK, reply_size};
}
