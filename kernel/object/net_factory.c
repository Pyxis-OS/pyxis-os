#include <abi/echo.h>
#include <abi/net_factory.h>
#include <abi/tcp.h>
#include <abi/udp.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/net_factory.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static uint64_t operation_rights(const struct net_factory *factory)
{
  switch (factory->object.type) {
  case OBJECT_TCP_SERVICE: return TCP_SERVICE_RIGHTS;
  case OBJECT_UDP_SERVICE: return UDP_SERVICE_RIGHT_OPEN | UDP_SERVICE_RIGHT_BROADCAST;
  case OBJECT_ECHO: return ECHO_RIGHT_SEND;
  default: KASSERT(false);
  }
}

static enum call_status grant_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_LIMIT:
  case CAP_FULL: return CALL_LIMIT;
  default: KASSERT(false);
  }
}

struct syscall_result net_factory_next_interface_call(struct net_factory *factory,
    uint64_t rights, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & operation_rights(factory))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size != sizeof(uint64_t) ||
      reply_capacity < sizeof(struct net_factory_next_interface_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t after_id;
  if (!copy_from_user(&after_id, request_address, sizeof(after_id)) ||
      !user_buffer_check(reply_address, sizeof(struct net_factory_next_interface_reply),
          USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct net_factory_next_interface_reply reply = {0};
  for (size_t i = 0; i < factory->interface_count; ++i) {
    uint64_t id = factory->interfaces[i]->id;
    if (id > after_id && (!reply.interface_id || id < reply.interface_id)) {
      reply.interface_id = id;
    }
  }
  if (!task_operation_start()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

void net_factory_derive_execute(struct net_factory_derive_service_request *request)
{
  struct kernel_object *object = net_factory_create(request->type, &request->interface, 1);
  request->status = CALL_NO_MEMORY;
  if (!object) {
    return;
  }
  struct capability_grant grant = {0};
  enum capability_result result = capability_grant_retain(object, request->rights, 0, &grant);
  if (result == CAP_OK) {
    result = forwarder_derive_grant(&grant, task_forwarder_operation());
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, &grant, 1);
  }
  if (result == CAP_OK && !task_operation_start()) {
    result = CAP_DENIED;
  }
  if (result == CAP_OK) {
    capability_install_reserved(&request->reservation, &request->slot, &grant, 1,
        &request->result);
  }
  request->status = grant_status(result);
  capability_grant_release(&grant);
  object_release(object);
}

struct syscall_result net_factory_derive_call(struct net_factory *factory,
    uint64_t rights, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & operation_rights(factory))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct net_factory_derive_request input = {0};
  size_t payload_size = sizeof(input) - sizeof(input.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct net_factory_derive_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&input.interface_id, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct net_factory_derive_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!input.rights || (input.rights & ~(rights & operation_rights(factory)))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  const struct net_interface *interface = NULL;
  for (size_t i = 0; i < factory->interface_count; ++i) {
    if (factory->interfaces[i]->id == input.interface_id) {
      interface = factory->interfaces[i];
      break;
    }
  }
  if (!interface) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum capability_result result = capability_request_reservation(1, &reservation, &slot);
  if (result != CAP_OK) {
    return (struct syscall_result){grant_status(result), 0};
  }
  if (task_stop_requested() || task_operation_denied()) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){task_operation_denied() ? CALL_DENIED : CALL_ENDPOINT_CLOSED, 0};
  }
  struct net_factory_derive_service_request *request =
      (struct net_factory_derive_service_request *)bsp_request_prepare(BSP_SERVICE_NET_FACTORY);
  request->reservation = reservation;
  request->slot = slot;
  reservation = (struct capability_reservation){0};
  request->interface = interface;
  request->type = factory->object.type;
  request->rights = input.rights;
  request->result = HANDLE_INVALID;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  struct net_factory_derive_reply reply = {.handle = request->result};
  capability_reservation_release(&request->reservation, &request->slot);
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
