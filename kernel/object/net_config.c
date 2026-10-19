#include <abi/echo.h>
#include <abi/tcp.h>
#include <abi/udp.h>
#include <kernel/mm/heap.h>
#include <kernel/net/factory.h>
#include <kernel/net/lwip.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/net_config.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <kernel/memory.h>

static void destroy_control(struct kernel_object *object)
{
  kfree(object);
}

static struct kernel_object *create_control(enum object_type type,
    struct net_interface *interface)
{
  struct net_control_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(&object->object, type, destroy_control);
    object->interface = interface;
  }
  return object ? &object->object : NULL;
}

struct kernel_object *net_registry_create(void)
{
  return create_control(OBJECT_NET_REGISTRY, NULL);
}

struct kernel_object *net_policy_create(void)
{
  return create_control(OBJECT_NET_POLICY, NULL);
}

static enum call_status grant_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_FULL:
  case CAP_LIMIT: return CALL_LIMIT;
  default: KASSERT(false);
  }
}

static void release_backing(struct net_control *control)
{
  if (control->protocol == PROTOCOL_NET_POLICY && control->operation == NET_POLICY_DEFINE &&
      control->entries) {
    for (size_t i = 0; i < control->count; ++i) {
      struct net_config_entry *entry = &control->entries[i];
      if (entry->interface && !entry->published) {
        kfree(entry->interface->tcp_projection);
        kfree(entry->interface);
      }
    }
  }
  kfree(control->entries);
  kfree(control->interfaces);
  control->entries = NULL;
  control->interfaces = NULL;
}

static enum call_status allocate_backing(struct net_control *control)
{
  if (!control->count) {
    return CALL_OK;
  }
  if (control->operation == NET_POLICY_FACTORY) {
    control->interfaces = kmalloc(control->count * sizeof(*control->interfaces));
    if (!control->interfaces) {
      return CALL_NO_MEMORY;
    }
  }
  control->entries = kmalloc(control->count * sizeof(*control->entries));
  if (!control->entries) {
    return CALL_NO_MEMORY;
  }
  memset(control->entries, 0, control->count * sizeof(*control->entries));
  if (control->operation != NET_POLICY_DEFINE) {
    return CALL_OK;
  }
  for (size_t i = 0; i < control->count; ++i) {
    struct net_interface *interface = kmalloc(sizeof(*interface) + 16);
    if (!interface) {
      return CALL_NO_MEMORY;
    }
    *interface = (struct net_interface){.name = (char *)(interface + 1)};
    control->entries[i].interface = interface;
    interface->tcp_projection = kmalloc(net_lwip_projection_size());
    if (!interface->tcp_projection) {
      return CALL_NO_MEMORY;
    }
    memset(interface->tcp_projection, 0, net_lwip_projection_size());
  }
  return CALL_OK;
}

void net_control_execute(struct net_control_service_request *request)
{
  if (request->stage == NET_CONTROL_ALLOCATE) {
    request->status = allocate_backing(&request->control);
    return;
  }
  if (request->stage == NET_CONTROL_RELEASE) {
    release_backing(&request->control);
    return;
  }
  struct kernel_object *object = request->type == OBJECT_NET_INTERFACE ?
    create_control(OBJECT_NET_INTERFACE, request->control.interface) :
    net_factory_create(request->type, request->control.interfaces, request->control.count);
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

static enum call_status backing_exchange(struct net_control *control,
    enum net_control_stage stage)
{
  struct net_control_service_request *request =
    (struct net_control_service_request *)bsp_request_prepare(BSP_SERVICE_NET_CONTROL);
  request->stage = stage;
  request->control = *control;
  request->status = CALL_OK;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  *control = request->control;
  bsp_request_release(&request->request);
  return status;
}

static struct syscall_result return_handle(struct net_control *control,
    enum object_type type, uint64_t rights, uintptr_t reply_address)
{
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
  struct net_control_service_request *request =
    (struct net_control_service_request *)bsp_request_prepare(BSP_SERVICE_NET_CONTROL);
  request->stage = NET_CONTROL_INSTALL;
  request->control = *control;
  request->reservation = reservation;
  request->slot = slot;
  request->type = type;
  request->rights = rights;
  request->result = HANDLE_INVALID;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  struct net_handle_reply reply = {.handle = request->result};
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

static enum object_type factory_type(uint64_t protocol, uint64_t rights)
{
  switch (protocol) {
  case PROTOCOL_TCP_SERVICE:
    return rights && !(rights & ~TCP_SERVICE_RIGHTS) ? OBJECT_TCP_SERVICE : 0;
  case PROTOCOL_UDP_SERVICE:
    return rights && !(rights & ~(UDP_SERVICE_RIGHT_OPEN | UDP_SERVICE_RIGHT_BROADCAST)) ?
      OBJECT_UDP_SERVICE : 0;
  case PROTOCOL_ECHO:
    return rights && !(rights & ~ECHO_RIGHT_SEND) ? OBJECT_ECHO : 0;
  default: return 0;
  }
}

struct syscall_result net_control_call(struct net_control_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  struct net_control control = {.operation = operation, .interface = object->interface};
  void *input = NULL;
  size_t input_size = 0, reply_size = 0;
  const void *output = NULL;
  uint64_t required = 0, entries_address = 0;
  enum object_type returned_type = 0;
  uint64_t returned_rights = 0;
  struct net_controller_request controller = {0};
  struct net_policy_batch_request batch = {0};
  struct net_policy_interface_request facet = {0};
  struct net_policy_factory_request factory = {0};
  switch (object->object.type) {
  case OBJECT_NET_REGISTRY:
    control.protocol = PROTOCOL_NET_REGISTRY;
    required = NET_REGISTRY_RIGHT_READ;
    switch (operation) {
    case NET_REGISTRY_NEXT_CONTROLLER:
      input = &controller.after_id;
      input_size = sizeof(controller) - sizeof(controller.header);
      output = &control.controller_reply;
      reply_size = sizeof(control.controller_reply);
      break;
    case NET_REGISTRY_NEXT_INTERFACE:
      input = &control.after_id;
      input_size = sizeof(control.after_id);
      output = &control.interface_reply;
      reply_size = sizeof(control.interface_reply);
      break;
    case NET_REGISTRY_LOOKUP:
      input = &control.after_id;
      input_size = sizeof(control.after_id);
      reply_size = sizeof(struct net_handle_reply);
      returned_type = OBJECT_NET_INTERFACE;
      returned_rights = NET_INTERFACE_RIGHT_READ;
      break;
    case NET_REGISTRY_POLICY:
      output = &control.policy_reply;
      reply_size = sizeof(control.policy_reply);
      break;
    default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    break;
  case OBJECT_NET_INTERFACE:
    control.protocol = PROTOCOL_NET_INTERFACE;
    required = operation == NET_INTERFACE_QUERY ? NET_INTERFACE_RIGHT_READ : NET_INTERFACE_RIGHT_WRITE;
    switch (operation) {
    case NET_INTERFACE_QUERY:
      output = &control.interface_reply;
      reply_size = sizeof(control.interface_reply);
      break;
    case NET_INTERFACE_REPLACE:
      input = &control.replace.address;
      input_size = sizeof(control.replace) - sizeof(control.replace.header);
      break;
    case NET_INTERFACE_CLEAR: break;
    case NET_INTERFACE_REPORT_DHCP:
      input = &control.report.address_epoch;
      input_size = sizeof(control.report) - sizeof(control.report.header);
      break;
    default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    break;
  case OBJECT_NET_POLICY:
    control.protocol = PROTOCOL_NET_POLICY;
    required = NET_POLICY_RIGHT_MANAGE;
    switch (operation) {
    case NET_POLICY_DEFINE:
    case NET_POLICY_BIND:
      input = &batch.entries;
      input_size = sizeof(batch) - sizeof(batch.header);
      break;
    case NET_POLICY_INTERFACE:
      input = &facet.interface_id;
      input_size = sizeof(facet) - sizeof(facet.header);
      returned_type = OBJECT_NET_INTERFACE;
      reply_size = sizeof(struct net_handle_reply);
      break;
    case NET_POLICY_FACTORY:
      input = &factory.protocol;
      input_size = sizeof(factory) - sizeof(factory.header);
      reply_size = sizeof(struct net_handle_reply);
      break;
    case NET_POLICY_REPLACE:
      input = &control.policy.default_interface_id;
      input_size = sizeof(control.policy) - sizeof(control.policy.header);
      break;
    default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    break;
  default: KASSERT(false);
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size != input_size || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if ((input_size && !copy_from_user(input, request_address, input_size)) ||
      (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (controller.reserved) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (control.protocol == PROTOCOL_NET_REGISTRY && operation == NET_REGISTRY_NEXT_CONTROLLER) {
    control.after_id = controller.after_id;
  }
  if (control.protocol == PROTOCOL_NET_POLICY) {
    if (operation == NET_POLICY_INTERFACE) {
      if (!facet.rights || (facet.rights & ~NET_INTERFACE_RIGHTS)) {
        return (struct syscall_result){CALL_DENIED, 0};
      }
      control.after_id = facet.interface_id;
      control.rights = returned_rights = facet.rights;
    } else if (operation == NET_POLICY_FACTORY) {
      returned_type = factory_type(factory.protocol, factory.rights);
      if (!returned_type) {
        return (struct syscall_result){CALL_BAD_REQUEST, 0};
      }
      returned_rights = factory.rights;
      control.count = factory.count;
      entries_address = factory.interface_ids;
    } else if (operation == NET_POLICY_DEFINE || operation == NET_POLICY_BIND) {
      control.count = batch.count;
      entries_address = batch.entries;
    }
  }
  bool has_batch = control.protocol == PROTOCOL_NET_POLICY &&
    (operation == NET_POLICY_DEFINE || operation == NET_POLICY_BIND || operation == NET_POLICY_FACTORY);
  if (has_batch && (!control.count || control.count > 255)) {
    return (struct syscall_result){control.count ? CALL_LIMIT : CALL_BAD_REQUEST, 0};
  }
  enum call_status status = CALL_OK;
  if (has_batch) {
    status = backing_exchange(&control, NET_CONTROL_ALLOCATE);
    if (status == CALL_OK) {
      size_t record_size = operation == NET_POLICY_DEFINE ? sizeof(struct net_interface_definition) :
        operation == NET_POLICY_BIND ? sizeof(struct net_interface_binding) : sizeof(uint64_t);
      for (size_t i = 0; i < control.count; ++i) {
        void *record = operation == NET_POLICY_DEFINE ? (void *)&control.entries[i].definition :
          operation == NET_POLICY_BIND ? (void *)&control.entries[i].binding :
          (void *)&control.entries[i].binding.interface_id;
        if (entries_address > UINTPTR_MAX - i * record_size ||
            !copy_from_user(record, entries_address + i * record_size, record_size)) {
          status = CALL_BAD_BUFFER;
          break;
        }
      }
    }
  }
  if (status == CALL_OK) {
    status = net_config_exchange(&control);
  }
  struct syscall_result result = {status, 0};
  if (status == CALL_OK && returned_type) {
    result = return_handle(&control, returned_type, returned_rights, reply_address);
  } else if (status == CALL_OK && reply_size) {
    result = copy_to_user(reply_address, output, reply_size) ?
      (struct syscall_result){CALL_OK, reply_size} : (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (has_batch) {
    backing_exchange(&control, NET_CONTROL_RELEASE);
  }
  return result;
}
