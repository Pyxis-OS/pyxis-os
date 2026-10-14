#include <arch/smp.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/namespace.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/string.h>
#include <kernel/user_memory.h>

struct namespace_binding {
  char name[NAMESPACE_NAME_MAX + 1];
  struct capability_grant grant;
};

struct namespace_object {
  struct kernel_object object;
  atomic_bool locked;
  struct namespace_binding bindings[NAMESPACE_BINDINGS_MAX];
};

/* IF=0. Only bounded kernel copies/reference operations while held; never
 * user copying, allocation or capability growth. Mutation elects its operation
 * start under the forwarder guard; logical grant releases happen after unlock. */
static void lock_namespace(struct namespace_object *namespace)
{
  while (atomic_exchange_explicit(&namespace->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_namespace(struct namespace_object *namespace)
{
  atomic_store_explicit(&namespace->locked, false, memory_order_release);
}

static enum call_status grant_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  default: panic("unexpected namespace capability result %u", (unsigned)result);
  }
}

static struct namespace_binding *find_binding(struct namespace_object *namespace,
    const char *name)
{
  size_t length = strnlen(name, NAMESPACE_NAME_MAX + 1);
  if (length > NAMESPACE_NAME_MAX) {
    return NULL;
  }
  for (size_t i = 0; i < NAMESPACE_BINDINGS_MAX; ++i) {
    struct namespace_binding *binding = &namespace->bindings[i];
    if (binding->grant.object && memcmp(binding->name, name, length + 1) == 0) {
      return binding;
    }
  }
  return NULL;
}

bool namespace_has_name(struct kernel_object *object, const char *name)
{
  KASSERT(object->type == OBJECT_NAMESPACE);
  struct namespace_object *namespace = (struct namespace_object *)object;
  lock_namespace(namespace);
  bool found = find_binding(namespace, name) != NULL;
  unlock_namespace(namespace);
  return found;
}

static bool name_valid(const char name[NAMESPACE_NAME_MAX + 1])
{
  size_t i = 0;
  for (; i <= NAMESPACE_NAME_MAX && name[i]; ++i) {
    char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '+')) {
      return false;
    }
  }
  if (!i || i > NAMESPACE_NAME_MAX) {
    return false;
  }
  for (; i <= NAMESPACE_NAME_MAX; ++i) {
    if (name[i]) {
      return false;
    }
  }
  return true;
}

static void destroy_namespace(struct kernel_object *object)
{
  struct namespace_object *namespace = (struct namespace_object *)object;
  for (size_t i = 0; i < NAMESPACE_BINDINGS_MAX; ++i) {
    if (namespace->bindings[i].grant.object) {
      capability_grant_release(&namespace->bindings[i].grant);
    }
  }
  kfree(namespace);
}

static void destroy_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *namespace_service_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *service = kmalloc(sizeof(*service));
  if (service) {
    object_init(service, OBJECT_NAMESPACE_SERVICE, destroy_service);
  }
  return service;
}

static enum call_status namespace_create(struct namespace_create_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  request->handle = HANDLE_INVALID;
  struct namespace_object *namespace = kmalloc(sizeof(*namespace));
  if (!namespace) {
    return CALL_NO_MEMORY;
  }
  memset(namespace, 0, sizeof(*namespace));
  atomic_init(&namespace->locked, false);
  object_init(&namespace->object, OBJECT_NAMESPACE, destroy_namespace);
  struct capability_grant grant;
  enum capability_result result = capability_grant_retain(&namespace->object,
      NAMESPACE_RIGHTS, 0, &grant);
  if (result == CAP_OK) {
    result = forwarder_derive_grant(&grant, request->request.operation);
  }
  if (result == CAP_OK && !task_operation_start()) {
    result = CAP_DENIED;
  }
  if (result == CAP_OK) {
    KASSERT(capability_validate_grants(request->reservation.table, &grant, 1) == CAP_OK);
    capability_install_reserved(&request->reservation, &request->slot, &grant, 1,
        &request->handle);
  }
  capability_grant_release(&grant);
  object_release(&namespace->object);
  return grant_status(result);
}

void namespace_create_execute(struct namespace_create_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->reservation.table);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  request->result = namespace_create(request);
  capability_reservation_release(&request->reservation, &request->slot);
}

static enum call_status create_namespace(handle_t *handle)
{
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
  if (reserved != CAP_OK) {
    return grant_status(reserved);
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return CALL_ENDPOINT_CLOSED;
  }
  struct namespace_create_request *request =
      (struct namespace_create_request *)bsp_request_prepare(BSP_SERVICE_NAMESPACE_CREATE);
  struct process *process = process_current();
  KASSERT(process);
  request->reservation = reservation;
  request->slot = slot;
  reservation = (struct capability_reservation){0};
  bsp_request_submit_and_wait(&request->request);
  *handle = request->handle;
  enum call_status result = request->result;
  bsp_request_release(&request->request);
  return result;
}

struct syscall_result namespace_service_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != NAMESPACE_CREATE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & NAMESPACE_SERVICE_RIGHT_CREATE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size || reply_capacity < sizeof(struct namespace_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct namespace_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct namespace_reply reply;
  enum call_status status = create_namespace(&reply.handle);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static enum call_status bind_client(struct namespace_object *namespace,
    uint64_t operation, const struct namespace_bind_message *message)
{
  struct capability_grant grant;
  enum capability_result result = capability_grant_acquire(&process_current()->capabilities,
      message->client, message->rights, message->transport, &grant);
  if (result != CAP_OK) {
    return grant_status(result);
  }
  if (object_effective_type(grant.object) != OBJECT_ENDPOINT_EXPORT) {
    capability_grant_release(&grant);
    return CALL_WRONG_TYPE;
  }
  enum call_status available = forwarder_status(grant.object);
  if (available != CALL_OK) {
    capability_grant_release(&grant);
    return available;
  }

  struct capability_grant old = {0};
  enum call_status status = CALL_OK;
  lock_namespace(namespace);
  if (!task_operation_start()) {
    unlock_namespace(namespace);
    capability_grant_release(&grant);
    return CALL_DENIED;
  }
  struct namespace_binding *binding = find_binding(namespace, message->name);
  if (binding && operation == NAMESPACE_PUBLISH) {
    status = CALL_ALREADY_EXISTS;
  } else if (!binding && operation == NAMESPACE_REPLACE) {
    status = CALL_NOT_FOUND;
  } else {
    if (!binding) {
      for (size_t i = 0; i < NAMESPACE_BINDINGS_MAX; ++i) {
        if (!namespace->bindings[i].grant.object) {
          binding = &namespace->bindings[i];
          break;
        }
      }
    }
    if (!binding) {
      status = CALL_LIMIT;
    } else {
      old = binding->grant;
      memcpy(binding->name, message->name, sizeof(binding->name));
      binding->grant = grant;
      grant = (struct capability_grant){0};
    }
  }
  unlock_namespace(namespace);
  capability_grant_release(&grant);
  capability_grant_release(&old);
  return status;
}

static enum call_status remove_binding(struct namespace_object *namespace,
    const char *name)
{
  lock_namespace(namespace);
  if (!task_operation_start()) {
    unlock_namespace(namespace);
    return CALL_DENIED;
  }
  struct namespace_binding *binding = find_binding(namespace, name);
  struct capability_grant old = binding ? binding->grant : (struct capability_grant){0};
  if (binding) {
    memset(binding, 0, sizeof(*binding));
  }
  unlock_namespace(namespace);
  if (!old.object) {
    return CALL_NOT_FOUND;
  }
  capability_grant_release(&old);
  return CALL_OK;
}

static enum call_status lookup_binding(struct namespace_object *namespace,
    const char *name, handle_t *handle)
{
  *handle = HANDLE_INVALID;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
  if (reserved != CAP_OK) {
    return grant_status(reserved);
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return CALL_ENDPOINT_CLOSED;
  }
  lock_namespace(namespace);
  struct namespace_binding *binding = find_binding(namespace, name);
  if (!binding) {
    unlock_namespace(namespace);
    capability_reservation_release(&reservation, &slot);
    return CALL_NOT_FOUND;
  }
  struct kernel_object *client = binding->grant.object;
  uint64_t rights = binding->grant.rights, transport = binding->grant.transport;
  bool retained = object_retain(client);
  unlock_namespace(namespace);
  if (!retained) {
    capability_reservation_release(&reservation, &slot);
    return CALL_LIMIT;
  }
  /* Capture is lookup's linearization point. Replacement/removal after this
   * point cannot change the returned object or its retained authority. */
  enum call_status status = forwarder_status(client);
  if (status == CALL_OK) {
    struct capability_grant grant;
    enum capability_result result = capability_grant_retain(client, rights, transport, &grant);
    if (result == CAP_OK) {
      result = forwarder_request_derivation(&grant);
    }
    if (result == CAP_OK && !task_operation_start()) {
      result = CAP_DENIED;
    }
    if (result == CAP_OK) {
      result = capability_validate_grants(reservation.table, &grant, 1);
    }
    if (result == CAP_OK) {
      capability_install_reserved(&reservation, &slot, &grant, 1, handle);
    }
    capability_grant_release(&grant);
    status = grant_status(result);
  }
  object_release(client);
  capability_reservation_release(&reservation, &slot);
  return status;
}

struct syscall_result namespace_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  bool bind = operation == NAMESPACE_PUBLISH || operation == NAMESPACE_REPLACE;
  if (!bind && operation != NAMESPACE_REMOVE && operation != NAMESPACE_LOOKUP) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  uint64_t required = operation == NAMESPACE_LOOKUP ? NAMESPACE_RIGHT_LOOKUP : NAMESPACE_RIGHT_MANAGE;
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct namespace_bind_message message = {0};
  size_t size = bind ? sizeof(message) : sizeof(struct namespace_name_message);
  size -= sizeof(struct message_header);
  if (request_size != size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user((uint8_t *)&message + sizeof(message.header), request_address, size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!name_valid(message.name)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct namespace_object *namespace = (struct namespace_object *)object;
  if (bind) {
    return (struct syscall_result){bind_client(namespace, operation, &message), 0};
  }
  if (operation == NAMESPACE_REMOVE) {
    return (struct syscall_result){remove_binding(namespace, message.name), 0};
  }
  if (reply_capacity < sizeof(struct namespace_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct namespace_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct namespace_reply reply;
  enum call_status status = lookup_binding(namespace, message.name, &reply.handle);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
