#include <abi/directory.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/command_search.h>
#include <kernel/object/environment.h>
#include <kernel/object/forwarder.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

struct command_list_entry {
  char label[COMMAND_LIST_LABEL_MAX + 1];
  struct capability_grant grant;
};

struct command_list {
  struct kernel_object object;
  /* IF=0. Elect logical ownership only; no callbacks, allocation or copies. */
  struct spinlock lock;
  size_t owners;
  size_t count;
  struct command_list_entry entries[];
};

static enum call_status grant_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  case CAP_INVALID: return CALL_BAD_REQUEST;
  default: panic("unexpected command list capability result %u", (unsigned)result);
  }
}

bool command_list_entry_valid(const struct command_list_create_entry *entry)
{
  uint64_t allowed = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE |
      DIRECTORY_RIGHT_READ_FILES;
  if ((entry->rights & ~allowed) || entry->transport) {
    return false;
  }
  size_t i = 0;
  while (i <= COMMAND_LIST_LABEL_MAX && entry->label[i]) {
    ++i;
  }
  if (i > COMMAND_LIST_LABEL_MAX) {
    return false;
  }
  for (; i <= COMMAND_LIST_LABEL_MAX; ++i) {
    if (entry->label[i]) {
      return false;
    }
  }
  return true;
}

bool command_list_grant_retain(struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_COMMAND_LIST);
  struct command_list *list = (struct command_list *)object;
  spin_lock(&list->lock);
  bool retained = list->owners && list->owners != SIZE_MAX;
  if (retained) {
    ++list->owners;
  }
  spin_unlock(&list->lock);
  return retained;
}

void command_list_grant_release(struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_COMMAND_LIST);
  struct command_list *list = (struct command_list *)object;
  spin_lock(&list->lock);
  KASSERT(list->owners);
  bool last = --list->owners == 0;
  spin_unlock(&list->lock);
  if (last) {
    for (size_t i = 0; i < list->count; ++i) {
      capability_grant_release(&list->entries[i].grant);
    }
  }
}

void command_list_release(struct kernel_object *object)
{
  command_list_grant_release(object);
  object_release(object);
}

static void destroy_list(struct kernel_object *object)
{
  struct command_list *list = (struct command_list *)object;
  KASSERT(!list->owners);
  kfree(list);
}

struct kernel_object *command_list_create(const struct command_list_create_entry *entries,
    struct capability_grant *grants, size_t count)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(count <= COMMAND_LIST_ENTRIES_LIMIT);
  struct command_list *list = kmalloc(sizeof(*list) + count * sizeof(list->entries[0]));
  if (!list) {
    return NULL;
  }
  object_init(&list->object, OBJECT_COMMAND_LIST, destroy_list);
  atomic_init(&list->lock.locked, false);
  list->owners = 1;
  list->count = count;
  for (size_t i = 0; i < count; ++i) {
    KASSERT(command_list_entry_valid(&entries[i]));
    KASSERT(object_effective_type(grants[i].object) == OBJECT_DIRECTORY);
    memcpy(list->entries[i].label, entries[i].label, sizeof(entries[i].label));
    list->entries[i].grant = grants[i];
    grants[i] = (struct capability_grant){0};
  }
  return &list->object;
}

struct syscall_result command_list_create_call(uintptr_t entries_address,
    size_t count, uintptr_t reply_address)
{
  if (count > COMMAND_LIST_ENTRIES_LIMIT) {
    return (struct syscall_result){CALL_LIMIT, 0};
  }
  struct command_list_create_entry entries[COMMAND_LIST_ENTRIES_LIMIT];
  struct capability_grant members[COMMAND_LIST_ENTRIES_LIMIT] = {0};
  if (count && !copy_from_user(entries, entries_address, count * sizeof(entries[0]))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  for (size_t i = 0; i < count; ++i) {
    if (!command_list_entry_valid(&entries[i])) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }
  enum call_status status = CALL_OK;
  for (size_t i = 0; i < count; ++i) {
    enum capability_result result = capability_grant_acquire(
        &process_current()->capabilities, entries[i].handle,
        entries[i].rights, entries[i].transport, &members[i]);
    if (result != CAP_OK) {
      status = grant_status(result);
      break;
    }
    if (object_effective_type(members[i].object) != OBJECT_DIRECTORY) {
      status = CALL_WRONG_TYPE;
      break;
    }
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (status == CALL_OK) {
    status = grant_status(capability_request_reservation(1, &reservation, &slot));
  }
  struct kernel_object *object = NULL;
  if (status == CALL_OK && task_stop_requested()) {
    status = CALL_ENDPOINT_CLOSED;
  }
  if (status == CALL_OK) {
    struct environment_request *request = (struct environment_request *)
        bsp_request_prepare(BSP_SERVICE_ENVIRONMENT);
    request->action = ENVIRONMENT_ALLOCATE_COMMAND_LIST;
    request->command_entries = entries;
    request->command_grants = members;
    request->count = count;
    bsp_request_submit_and_wait(&request->request);
    object = request->command_list;
    status = request->result;
    bsp_request_release(&request->request);
  }
  struct capability_grant grant = {0};
  struct command_list_create_reply reply = {0};
  if (status == CALL_OK) {
    enum capability_result result = capability_grant_retain(object,
        COMMAND_LIST_RIGHT_READ, 0, &grant);
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
      capability_install_reserved(&reservation, &slot, &grant, 1, &reply.handle);
    }
    status = grant_status(result);
  }
  capability_grant_release(&grant);
  if (object) {
    command_list_release(object);
  }
  for (size_t i = 0; i < count; ++i) {
    capability_grant_release(&members[i]);
  }
  capability_reservation_release(&reservation, &slot);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  return copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1) ?
      (struct syscall_result){CALL_OK, sizeof(reply)} :
      (struct syscall_result){CALL_BAD_BUFFER, 0};
}

static struct syscall_result get_entry(struct command_list *list,
    size_t index, uintptr_t reply_address)
{
  if (index >= list->count) {
    return (struct syscall_result){CALL_NOT_FOUND, 0};
  }
  /* A storage admission alone cannot keep directory grants alive after CLOSE.
   * Keep a provisional logical owner until the member clone is complete. */
  if (!command_list_grant_retain(&list->object)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct command_list_entry *entry = &list->entries[index];
  struct command_list_entry_reply reply = {
    .rights = entry->grant.rights, .transport = entry->grant.transport,
  };
  memcpy(reply.label, entry->label, sizeof(reply.label));
  struct capability_grant grant = {0};
  enum capability_result result = capability_grant_retain(entry->grant.object,
      entry->grant.rights, entry->grant.transport, &grant);
  command_list_grant_release(&list->object);
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (result == CAP_OK) {
    result = capability_request_reservation(1, &reservation, &slot);
  }
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
    capability_install_reserved(&reservation, &slot, &grant, 1, &reply.handle);
  }
  capability_grant_release(&grant);
  capability_reservation_release(&reservation, &slot);
  if (result != CAP_OK) {
    return (struct syscall_result){grant_status(result), 0};
  }
  return copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1) ?
      (struct syscall_result){CALL_OK, sizeof(reply)} :
      (struct syscall_result){CALL_BAD_BUFFER, 0};
}

struct syscall_result command_list_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & COMMAND_LIST_RIGHT_READ)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct command_list *list = (struct command_list *)object;
  if (operation == COMMAND_LIST_INFO) {
    struct command_list_info_reply reply = {list->count, COMMAND_LIST_ENTRIES_LIMIT};
    if (request_size || reply_capacity < sizeof(reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return copy_to_user(reply_address, &reply, sizeof(reply)) ?
        (struct syscall_result){CALL_OK, sizeof(reply)} :
        (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation != COMMAND_LIST_GET) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (request_size != sizeof(uint64_t) || reply_capacity < sizeof(struct command_list_entry_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t index;
  if (!copy_from_user(&index, request_address, sizeof(index)) ||
      !user_buffer_check(reply_address, sizeof(struct command_list_entry_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return get_entry(list, index, reply_address);
}
