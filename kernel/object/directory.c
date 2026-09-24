#include <abi/directory.h>
#include <abi/file.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/directory.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void destroy_directory(struct kernel_object *object)
{
  struct directory_object *directory = (struct directory_object *)object;
  struct directory_entry *entry = directory->first;
  while (entry) {
    struct directory_entry *next = entry->next;
    object_release(entry->object);
    kfree(entry);
    entry = next;
  }
  kfree(directory);
}

struct directory_object *directory_create(enum directory_backing backing)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(backing == DIRECTORY_INITRD || backing == DIRECTORY_RAM);
  struct directory_object *directory = kmalloc(sizeof(*directory));
  if (!directory) {
    return NULL;
  }
  *directory = (struct directory_object){.backing = backing, .generation = 1};
  atomic_init(&directory->locked, false);
  object_init(&directory->object, OBJECT_DIRECTORY, destroy_directory);
  return directory;
}

/* IF=0. Never allocate, sleep or acquire scheduler locks while held. */
static void lock_directory(struct directory_object *directory)
{
  while (atomic_exchange_explicit(&directory->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_directory(struct directory_object *directory)
{
  atomic_store_explicit(&directory->locked, false, memory_order_release);
}

static uint64_t entry_kind(const struct directory_entry *entry)
{
  return entry->object->type == OBJECT_DIRECTORY ? DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE;
}

static enum call_status check_name(uintptr_t address, size_t length)
{
  if (!length) {
    return CALL_BAD_REQUEST;
  }
  if (!user_buffer_check(address, length, USER_BUFFER_READ)) {
    return CALL_BAD_BUFFER;
  }

  /* Names have no fixed ABI length limit. Copy bounded chunks on the private
   * syscall stack; the single user task cannot modify this input concurrently. */
  char buffer[256];
  for (size_t offset = 0; offset < length;) {
    size_t count = length - offset;
    if (count > sizeof(buffer)) {
      count = sizeof(buffer);
    }
    KASSERT(copy_from_user(buffer, address + offset, count));
    if (!offset && ((length == 1 && buffer[0] == '.') ||
        (length == 2 && buffer[0] == '.' && buffer[1] == '.'))) {
      return CALL_BAD_REQUEST;
    }
    for (size_t i = 0; i < count; ++i) {
      if (!buffer[i] || buffer[i] == '/') {
        return CALL_BAD_REQUEST;
      }
    }
    offset += count;
  }
  return CALL_OK;
}

static struct directory_entry *find_user_entry(struct directory_object *directory,
                                                uintptr_t name, size_t length)
{
  char buffer[256];
  for (struct directory_entry *entry = directory->first; entry; entry = entry->next) {
    if (entry->name_length != length) {
      continue;
    }
    size_t offset = 0;
    while (offset < length) {
      size_t count = length - offset;
      if (count > sizeof(buffer)) {
        count = sizeof(buffer);
      }
      KASSERT(copy_from_user(buffer, name + offset, count));
      if (memcmp(buffer, entry->name + offset, count)) {
        break;
      }
      offset += count;
    }
    if (offset == length) {
      return entry;
    }
  }
  return NULL;
}

static enum call_status check_child_request(uint64_t rights,
    const struct directory_child_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  if (reply_capacity < sizeof(struct directory_child_reply) ||
      (request->kind != DIRECTORY_KIND_FILE && request->kind != DIRECTORY_KIND_DIRECTORY)) {
    return CALL_BAD_REQUEST;
  }
  if (!user_buffer_check(reply_address, sizeof(struct directory_child_reply), USER_BUFFER_WRITE)) {
    return CALL_BAD_BUFFER;
  }
  uint64_t allowed = rights;
  if (request->kind == DIRECTORY_KIND_FILE) {
    allowed = ((rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0) |
              ((rights & DIRECTORY_RIGHT_WRITE_FILES) ? FILE_RIGHT_WRITE : 0);
  }
  if (request->rights & ~allowed) {
    return CALL_DENIED;
  }
  return check_name(request->name, request->name_length);
}

/* Caller retains object while a table-growth loan may block. No directory
 * lock or capability-entry pointer survives the wait. */
static enum call_status install_child(struct kernel_object *object, uint64_t rights,
                                       handle_t *handle)
{
  struct capability_table *table = &process_current()->capabilities;
  enum capability_result result;
  for (;;) {
    result = capability_insert(table, object, rights, handle);
    if (result != CAP_FULL) {
      break;
    }
    result = task_grow_capabilities();
    if (result != CAP_OK) {
      break;
    }
  }
  if (result == CAP_OK) {
    return CALL_OK;
  }
  KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT);
  return result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
}

static struct syscall_result lookup(struct directory_object *directory, uint64_t rights,
    const struct directory_child_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  enum call_status status = check_child_request(rights, request, reply_address, reply_capacity);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }

  struct kernel_object *object = NULL;
  lock_directory(directory);
  struct directory_entry *entry = find_user_entry(directory, request->name, request->name_length);
  if (!entry) {
    status = CALL_NOT_FOUND;
  } else if (entry_kind(entry) != request->kind) {
    status = CALL_WRONG_TYPE;
  } else if (!object_retain(entry->object)) {
    status = CALL_LIMIT;
  } else {
    object = entry->object;
  }
  unlock_directory(directory);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }

  struct directory_child_reply reply;
  status = install_child(object, request->rights, &reply.handle);
  object_release(object);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* Checked private mappings stay writable while the sole task is blocked. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static bool name_exists(struct directory_object *directory, const struct directory_entry *candidate)
{
  for (struct directory_entry *entry = directory->first; entry; entry = entry->next) {
    if (entry->name_length == candidate->name_length &&
        !memcmp(entry->name, candidate->name, candidate->name_length)) {
      return true;
    }
  }
  return false;
}

static struct syscall_result create_child(struct directory_object *directory, uint64_t rights,
    const struct directory_child_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  enum call_status status = check_child_request(rights, request, reply_address, reply_capacity);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (directory->backing != DIRECTORY_RAM) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }
  if (request->name_length > SIZE_MAX - sizeof(struct directory_entry) - 1) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  lock_directory(directory);
  bool detached = directory->detached;
  bool exists = find_user_entry(directory, request->name, request->name_length) != NULL;
  unlock_directory(directory);
  if (detached) {
    return (struct syscall_result){CALL_NOT_FOUND, 0};
  }
  if (exists) {
    return (struct syscall_result){CALL_ALREADY_EXISTS, 0};
  }

  struct directory_entry *entry = task_allocate_directory_entry(request->kind, request->name_length);
  if (!entry) {
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  }
  /* The BSP never reads an AP's user address or private syscall stack. Its
   * allocation is unpublished; this task fills the already-validated name. */
  KASSERT(copy_from_user(entry->name, request->name, request->name_length));
  entry->name[entry->name_length] = 0;
  struct directory_child_reply reply;
  status = install_child(entry->object, request->rights, &reply.handle);
  if (status != CALL_OK) {
    task_discard_directory_entry(entry);
    return (struct syscall_result){status, 0};
  }

  lock_directory(directory);
  /* Another CPU may have created this name while allocation/table growth
   * slept. Publish only after both the entry and returned handle are ready. */
  if (directory->detached) {
    status = CALL_NOT_FOUND;
  } else if (name_exists(directory, entry)) {
    status = CALL_ALREADY_EXISTS;
  } else if (directory->generation == UINT64_MAX || directory->entry_count == SIZE_MAX) {
    status = CALL_LIMIT;
  } else {
    if (directory->last) {
      directory->last->next = entry;
    } else {
      directory->first = entry;
    }
    directory->last = entry;
    ++directory->entry_count;
    ++directory->generation;
  }
  unlock_directory(directory);

  if (status != CALL_OK) {
    KASSERT(capability_close(&process_current()->capabilities, reply.handle) == CAP_OK);
    task_discard_directory_entry(entry);
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result remove_child(struct directory_object *directory,
    const struct directory_remove_request *request)
{
  if (request->kind != DIRECTORY_KIND_ANY && request->kind != DIRECTORY_KIND_FILE &&
      request->kind != DIRECTORY_KIND_DIRECTORY) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  enum call_status status = check_name(request->name, request->name_length);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (directory->backing != DIRECTORY_RAM) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }

  lock_directory(directory);
  struct directory_entry *entry = find_user_entry(directory, request->name, request->name_length);
  if (!entry) {
    status = CALL_NOT_FOUND;
  } else if (request->kind != DIRECTORY_KIND_ANY && entry_kind(entry) != request->kind) {
    status = CALL_WRONG_TYPE;
  } else if (directory->generation == UINT64_MAX) {
    status = CALL_LIMIT;
  } else if (entry_kind(entry) == DIRECTORY_KIND_DIRECTORY) {
    /* There are no directory moves or links. Parent-before-child locking is
     * acyclic, and serializes the empty check with creation in the child. */
    struct directory_object *child = (struct directory_object *)entry->object;
    lock_directory(child);
    if (child->entry_count) {
      status = CALL_NOT_EMPTY;
    } else {
      child->detached = true;
    }
    unlock_directory(child);
  }

  if (status == CALL_OK) {
    struct directory_entry *previous = NULL;
    struct directory_entry **link = &directory->first;
    while (*link != entry) {
      previous = *link;
      link = &previous->next;
    }
    *link = entry->next;
    if (directory->last == entry) {
      directory->last = previous;
    }
    entry->next = NULL;
    --directory->entry_count;
    ++directory->generation;
  }
  unlock_directory(directory);

  if (status == CALL_OK) {
    /* The removed entry still owns its child until BSP disposal. Independent
     * handles keep the object alive after that reference is released. */
    task_discard_directory_entry(entry);
  }
  return (struct syscall_result){status, 0};
}

static struct syscall_result enumerate(struct directory_object *directory,
    const struct directory_enumerate_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct directory_enumerate_reply reply = {.cursor = request->cursor};
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      !user_buffer_check(request->name, request->capacity, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request->capacity && request->name < reply_address + sizeof(reply) &&
      reply_address < request->name + request->capacity) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!request->cursor.generation && request->cursor.position) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct directory_entry *entry = NULL;
  lock_directory(directory);
  if (request->cursor.generation && request->cursor.generation != directory->generation) {
    reply.outcome = DIRECTORY_CHANGED;
  } else {
    if (request->cursor.position > directory->entry_count) {
      unlock_directory(directory);
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    entry = directory->first;
    for (size_t i = 0; i < request->cursor.position; ++i) {
      entry = entry->next;
    }
    if (!entry) {
      reply.outcome = DIRECTORY_END;
      reply.cursor.generation = directory->generation;
    } else {
      reply.kind = entry_kind(entry);
      reply.name_size = entry->name_length + 1;
      if (request->capacity < reply.name_size) {
        reply.outcome = DIRECTORY_BUFFER_TOO_SMALL;
      } else {
        reply.outcome = DIRECTORY_ENTRY;
        reply.cursor.generation = directory->generation;
        ++reply.cursor.position;
      }
    }
  }
  /* Stable, checked private mappings make this copy nonblocking. Keep the
   * lock until it finishes so removal cannot reclaim the selected name. */
  if (reply.outcome == DIRECTORY_ENTRY) {
    KASSERT(copy_to_user(request->name, entry->name, reply.name_size));
  }
  unlock_directory(directory);
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result directory_call(struct directory_object *directory, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case DIRECTORY_LOOKUP:
    required = DIRECTORY_RIGHT_LOOKUP;
    break;
  case DIRECTORY_ENUMERATE:
    required = DIRECTORY_RIGHT_ENUMERATE;
    break;
  case DIRECTORY_CREATE:
    required = DIRECTORY_RIGHT_CREATE;
    break;
  case DIRECTORY_REMOVE:
    required = DIRECTORY_RIGHT_REMOVE;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  union directory_payload request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == DIRECTORY_LOOKUP) {
    return lookup(directory, rights, &request.lookup, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_CREATE) {
    return create_child(directory, rights, &request.create, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_REMOVE) {
    return remove_child(directory, &request.remove);
  }
  return enumerate(directory, &request.enumerate, reply_address, reply_capacity);
}
