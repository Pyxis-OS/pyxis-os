#include <abi/udp.h>
#include <kernel/mm/heap.h>
#include <kernel/net/udp.h>
#include <kernel/object/udp.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

static void destroy_udp_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *udp_service_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_UDP_SERVICE, destroy_udp_service);
  }
  return object;
}

struct syscall_result udp_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != UDP_OPEN) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & UDP_SERVICE_RIGHT_OPEN)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct udp_open_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct udp_open_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.address, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct udp_open_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.reserved) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct udp_open_reply reply;
  enum call_status status = net_udp_open(&process_current()->capabilities,
      request.address, request.port, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The sole task's checked mappings remain valid across the table loan. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result udp_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != UDP_INSPECT && operation != UDP_SHUTDOWN) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  uint64_t required = operation == UDP_INSPECT ? UDP_RIGHT_INSPECT : UDP_RIGHT_SHUTDOWN;
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  size_t reply_size = operation == UDP_INSPECT ? sizeof(struct udp_endpoint_info) : 0;
  if (request_size || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct udp_endpoint_info reply;
  enum call_status status = operation == UDP_INSPECT ?
      net_udp_inspect(object, &reply) : net_udp_shutdown(object);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (reply_size) {
    KASSERT(copy_to_user(reply_address, &reply, reply_size));
  }
  return (struct syscall_result){CALL_OK, reply_size};
}
