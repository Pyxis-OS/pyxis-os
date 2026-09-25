#include <abi/net_config.h>
#include <kernel/mm/heap.h>
#include <kernel/net/config.h>
#include <kernel/object/net_config.h>
#include <kernel/panic.h>
#include <kernel/user_memory.h>

static void destroy_net_config(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *net_config_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_NET_CONFIG, destroy_net_config);
  }
  return object;
}

struct syscall_result net_config_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != NET_CONFIG_QUERY && operation != NET_CONFIG_REPLACE && operation != NET_CONFIG_CLEAR) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  uint64_t required = operation == NET_CONFIG_QUERY ? NET_CONFIG_RIGHT_READ : NET_CONFIG_RIGHT_WRITE;
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct net_config_request request = {0};
  size_t payload_size = operation == NET_CONFIG_REPLACE ? sizeof(request) - sizeof(request.header) : 0;
  size_t reply_size = operation == NET_CONFIG_QUERY ? sizeof(struct net_config_reply) : 0;
  if (request_size != payload_size || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if ((payload_size && !copy_from_user(&request.address, request_address, payload_size)) ||
      (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.reserved) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct net_config_reply reply;
  enum call_status status = net_config_exchange(operation, &request, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The blocked task keeps its grant and mappings alive until completion. */
  if (reply_size) {
    KASSERT(copy_to_user(reply_address, &reply, reply_size));
  }
  return (struct syscall_result){CALL_OK, reply_size};
}
