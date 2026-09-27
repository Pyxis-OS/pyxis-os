#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/namespace.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

struct namespace_binding {
  char name[NAMESPACE_NAME_MAX + 1];
  struct kernel_object *client;
  uint64_t rights, transport;
};

struct namespace_object {
  struct kernel_object object;
  atomic_bool locked;
  struct namespace_binding bindings[NAMESPACE_BINDINGS_MAX];
};

/* IF=0. Only bounded kernel copies/reference operations while held; never
 * user copying, allocation, capability growth or another object lock. */
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
    if (binding->client && memcmp(binding->name, name, length + 1) == 0) {
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
    if (namespace->bindings[i].client) {
      object_release(namespace->bindings[i].client);
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

enum call_status namespace_create(struct process *owner, handle_t *handle)
{
  KASSERT(arch_cpu_index() == 0);
  *handle = HANDLE_INVALID;
  struct namespace_object *namespace = kmalloc(sizeof(*namespace));
  if (!namespace) {
    return CALL_NO_MEMORY;
  }
  memset(namespace, 0, sizeof(*namespace));
  atomic_init(&namespace->locked, false);
  object_init(&namespace->object, OBJECT_NAMESPACE, destroy_namespace);
  enum capability_result result = capability_install(&owner->capabilities,
      &namespace->object, NAMESPACE_RIGHTS, 0, handle);
  object_release(&namespace->object);
  return grant_status(result);
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
  enum call_status status = task_create_namespace(&reply.handle);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static enum call_status bind_client(struct namespace_object *namespace,
    uint64_t operation, const struct namespace_bind_message *message)
{
  struct kernel_object *client;
  enum capability_result result = capability_resolve(&process_current()->capabilities,
      message->client, message->rights, message->transport, &client, NULL, NULL);
  if (result != CAP_OK) {
    return grant_status(result);
  }
  if (client->type != OBJECT_ENDPOINT_EXPORT) {
    return CALL_WRONG_TYPE;
  }
  if (!endpoint_export_available(client)) {
    return CALL_ENDPOINT_CLOSED;
  }
  if (!object_retain(client)) {
    return CALL_LIMIT;
  }

  struct kernel_object *old = NULL;
  enum call_status status = CALL_OK;
  lock_namespace(namespace);
  struct namespace_binding *binding = find_binding(namespace, message->name);
  if (binding && operation == NAMESPACE_PUBLISH) {
    status = CALL_ALREADY_EXISTS;
  } else if (!binding && operation == NAMESPACE_REPLACE) {
    status = CALL_NOT_FOUND;
  } else {
    if (!binding) {
      for (size_t i = 0; i < NAMESPACE_BINDINGS_MAX; ++i) {
        if (!namespace->bindings[i].client) {
          binding = &namespace->bindings[i];
          break;
        }
      }
    }
    if (!binding) {
      status = CALL_LIMIT;
    } else {
      old = binding->client;
      memcpy(binding->name, message->name, sizeof(binding->name));
      binding->rights = message->rights;
      binding->transport = message->transport;
      binding->client = client;
    }
  }
  unlock_namespace(namespace);
  if (status != CALL_OK) {
    object_release(client);
  }
  if (old) {
    object_release(old);
  }
  return status;
}

static enum call_status remove_binding(struct namespace_object *namespace,
    const char *name)
{
  lock_namespace(namespace);
  struct namespace_binding *binding = find_binding(namespace, name);
  struct kernel_object *old = binding ? binding->client : NULL;
  if (binding) {
    memset(binding, 0, sizeof(*binding));
  }
  unlock_namespace(namespace);
  if (!old) {
    return CALL_NOT_FOUND;
  }
  object_release(old);
  return CALL_OK;
}

static enum call_status lookup_binding(struct namespace_object *namespace,
    const char *name, handle_t *handle)
{
  *handle = HANDLE_INVALID;
  lock_namespace(namespace);
  struct namespace_binding *binding = find_binding(namespace, name);
  if (!binding) {
    unlock_namespace(namespace);
    return CALL_NOT_FOUND;
  }
  struct kernel_object *client = binding->client;
  uint64_t rights = binding->rights, transport = binding->transport;
  bool retained = object_retain(client);
  unlock_namespace(namespace);
  if (!retained) {
    return CALL_LIMIT;
  }
  /* Capture is lookup's linearization point. Replacement/removal after this
   * point cannot change the returned object or its retained authority. */
  enum call_status status = CALL_ENDPOINT_CLOSED;
  if (endpoint_export_available(client)) {
    struct capability_table *table = &process_current()->capabilities;
    enum capability_result result;
    while ((result = capability_insert(table, client, rights, transport, handle)) == CAP_FULL) {
      result = task_grow_capabilities();
      if (result != CAP_OK) {
        break;
      }
    }
    status = grant_status(result);
  }
  object_release(client);
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
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
