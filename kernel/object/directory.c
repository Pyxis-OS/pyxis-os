#include <abi/directory.h>
#include <abi/file.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/fs/ramfs.h>
#include <kernel/mm/heap.h>
#include <kernel/object/directory.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

/* REMOVE may lock parent then child, while RENAME locks two arbitrary parents.
 * Serialize these multi-directory mutations before taking any directory lock.
 * Single-directory readers/creators never take this lock. No waits/allocations
 * may occur while it is held. */
static atomic_bool mutation_locked;

static void lock_mutation(void)
{
  while (atomic_exchange_explicit(&mutation_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_mutation(void)
{
  atomic_store_explicit(&mutation_locked, false, memory_order_release);
}

static void destroy_directory(struct kernel_object *object)
{
  struct directory_object *directory = (struct directory_object *)object;
  if (directory->backing == DIRECTORY_NPFS) {
    npfs_retire(directory->npfs);
    return;
  }
  if (directory->backing == DIRECTORY_HOST) {
    hostfs_retire(directory->host);
    return;
  }
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
  KASSERT(backing == DIRECTORY_INITRD || backing == DIRECTORY_RAM || backing == DIRECTORY_HOST);
  struct directory_object *directory = kmalloc(sizeof(*directory));
  if (!directory) {
    return NULL;
  }
  *directory = (struct directory_object){.backing = backing, .generation = 1};
  if ((backing == DIRECTORY_RAM || backing == DIRECTORY_INITRD) &&
      !fs_metadata_create(&directory->metadata, backing == DIRECTORY_INITRD)) {
    kfree(directory);
    return NULL;
  }
  atomic_init(&directory->locked, false);
  object_init(&directory->object, OBJECT_DIRECTORY, destroy_directory);
  return directory;
}

void directory_init_npfs(struct directory_object *directory, struct npfs_node *node)
{
  KASSERT(arch_cpu_index() == 0 && directory && node);
  *directory = (struct directory_object){.backing = DIRECTORY_NPFS, .npfs = node};
  atomic_init(&directory->locked, false);
  object_init(&directory->object, OBJECT_DIRECTORY, destroy_directory);
}

/* IF=0. Never allocate, sleep or acquire scheduler locks while held. */
static void lock_directory(struct directory_object *directory)
{
  KASSERT(directory->backing != DIRECTORY_NPFS);
  while (atomic_exchange_explicit(&directory->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_directory(struct directory_object *directory)
{
  atomic_store_explicit(&directory->locked, false, memory_order_release);
}

static void lock_parents(struct directory_object *source, struct directory_object *destination)
{
  lock_mutation();
  lock_directory(source);
  if (destination != source) {
    lock_directory(destination);
  }
}

static void unlock_parents(struct directory_object *source, struct directory_object *destination)
{
  if (destination != source) {
    unlock_directory(destination);
  }
  unlock_directory(source);
  unlock_mutation();
}

/* Caller holds the directory lock. Generation changes belong to the complete
 * operation: a same-directory rename advances it only once. */
static void unlink_entry(struct directory_object *directory, struct directory_entry *entry)
{
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
}

static void append_entry(struct directory_object *directory, struct directory_entry *entry)
{
  if (directory->last) {
    directory->last->next = entry;
  } else {
    directory->first = entry;
  }
  directory->last = entry;
  ++directory->entry_count;
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

static enum call_status check_child_request(struct directory_object *directory, uint64_t rights,
    const struct directory_child_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  if (reply_capacity < sizeof(struct directory_child_reply) ||
      (request->kind != DIRECTORY_KIND_FILE && request->kind != DIRECTORY_KIND_DIRECTORY)) {
    return CALL_BAD_REQUEST;
  }
  if (directory->backing == DIRECTORY_NPFS) {
    uint64_t mask = request->kind == DIRECTORY_KIND_FILE ? FILE_RIGHTS : DIRECTORY_RIGHTS;
    if (request->rights & ~mask) {
      return CALL_BAD_REQUEST;
    }
    if (request->name_length > NPFS_NAME_MAX) {
      return CALL_LIMIT;
    }
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
    result = capability_insert(table, object, rights, 0, handle);
    if (result != CAP_FULL) {
      break;
    }
    result = capability_request_growth();
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
  enum call_status status = check_child_request(directory, rights, request,
      reply_address, reply_capacity);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }

  struct kernel_object *object = NULL;
  if (directory->backing == DIRECTORY_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_LOOKUP);
    pending->job.node = directory->npfs;
    pending->job.rights = rights;
    pending->job.child_rights = request->rights;
    pending->job.kind = request->kind;
    pending->job.count = request->name_length;
    KASSERT(copy_from_user(pending->job.name, request->name, request->name_length));
    pending->job.name[request->name_length] = 0;
    npfs_request_submit_and_wait(pending);
    status = pending->job.status;
    object = pending->job.object;
    pending->job.object = NULL;
    npfs_request_release(pending);
  } else if (directory->backing == DIRECTORY_HOST) {
    if (request->name_length > VIRTIO_FS_NAME_MAX) {
      return (struct syscall_result){CALL_LIMIT, 0};
    }
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_LOOKUP);
    pending->node = directory->host;
    pending->kind = request->kind;
    pending->count = request->name_length;
    KASSERT(copy_from_user(pending->name, request->name, request->name_length));
    hostfs_request_submit_and_wait(pending);
    status = pending->status;
    object = pending->object;
    pending->object = NULL;
    hostfs_request_release(pending);
  } else {
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
  }
  if (status != CALL_OK) {
    if (object) {
      object_release(object);
    }
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
  enum call_status status = check_child_request(directory, rights, request,
      reply_address, reply_capacity);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (directory->backing == DIRECTORY_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_CREATE);
    pending->job.node = directory->npfs;
    pending->job.rights = rights;
    pending->job.child_rights = request->rights;
    pending->job.kind = request->kind;
    pending->job.count = request->name_length;
    pending->job.table = &process_current()->capabilities;
    KASSERT(copy_from_user(pending->job.name, request->name, request->name_length));
    pending->job.name[request->name_length] = 0;
    /* The blocked caller lends its table until the worker has installed the
     * returned handle and either published or unwound the namespace edit. */
    npfs_request_submit_and_wait(pending);
    status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    struct directory_child_reply reply = {.handle = pending->job.handle};
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    npfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (directory->backing == DIRECTORY_HOST) {
    if (request->name_length > VIRTIO_FS_NAME_MAX) {
      return (struct syscall_result){CALL_LIMIT, 0};
    }
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_CREATE);
    pending->node = directory->host;
    pending->kind = request->kind;
    pending->count = request->name_length;
    pending->table = &process_current()->capabilities;
    pending->rights = request->rights;
    KASSERT(copy_from_user(pending->name, request->name, request->name_length));
    hostfs_request_submit_and_wait(pending);
    status = pending->status;
    if (status != CALL_OK) {
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    struct directory_child_reply reply = {.handle = pending->handle};
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    hostfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply)};
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

  struct directory_entry *entry = ramfs_request_entry(request->kind, request->name_length);
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
    ramfs_request_discard(entry);
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
    append_entry(directory, entry);
    ++directory->generation;
    fs_metadata_touch(&directory->metadata);
  }
  unlock_directory(directory);

  if (status != CALL_OK) {
    KASSERT(capability_close(&process_current()->capabilities, reply.handle) == CAP_OK);
    ramfs_request_discard(entry);
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result remove_child(struct directory_object *directory, uint64_t rights,
    const struct directory_remove_request *request)
{
  if (request->kind != DIRECTORY_KIND_ANY && request->kind != DIRECTORY_KIND_FILE &&
      request->kind != DIRECTORY_KIND_DIRECTORY) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (directory->backing == DIRECTORY_NPFS && request->name_length > NPFS_NAME_MAX) {
    return (struct syscall_result){CALL_LIMIT, 0};
  }
  enum call_status status = check_name(request->name, request->name_length);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (directory->backing == DIRECTORY_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_REMOVE);
    pending->job.node = directory->npfs;
    pending->job.rights = rights;
    pending->job.kind = request->kind;
    pending->job.count = request->name_length;
    KASSERT(copy_from_user(pending->job.name, request->name, request->name_length));
    pending->job.name[request->name_length] = 0;
    npfs_request_submit_and_wait(pending);
    status = pending->job.status;
    npfs_request_release(pending);
    return (struct syscall_result){status, 0};
  }
  if (directory->backing == DIRECTORY_HOST) {
    if (request->name_length > VIRTIO_FS_NAME_MAX) {
      return (struct syscall_result){CALL_LIMIT, 0};
    }
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_REMOVE);
    pending->node = directory->host;
    pending->kind = request->kind;
    pending->count = request->name_length;
    KASSERT(copy_from_user(pending->name, request->name, request->name_length));
    hostfs_request_submit_and_wait(pending);
    status = pending->status;
    hostfs_request_release(pending);
    return (struct syscall_result){status, 0};
  }
  if (directory->backing != DIRECTORY_RAM) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }

  lock_mutation();
  lock_directory(directory);
  struct directory_entry *entry = find_user_entry(directory, request->name, request->name_length);
  if (!entry) {
    status = CALL_NOT_FOUND;
  } else if (request->kind != DIRECTORY_KIND_ANY && entry_kind(entry) != request->kind) {
    status = CALL_WRONG_TYPE;
  } else if (directory->generation == UINT64_MAX) {
    status = CALL_LIMIT;
  } else if (entry_kind(entry) == DIRECTORY_KIND_DIRECTORY) {
    /* The mutation lock excludes other two-directory operations. The child
     * lock serializes the empty check with creation in that directory. */
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
    unlink_entry(directory, entry);
    ++directory->generation;
    fs_metadata_touch(&directory->metadata);
  }
  unlock_directory(directory);
  unlock_mutation();

  if (status == CALL_OK) {
    /* The removed entry still owns its child until BSP disposal. Independent
     * handles keep the object alive after that reference is released. */
    ramfs_request_discard(entry);
  }
  return (struct syscall_result){status, 0};
}

/* Both parents locked. Returned entries are borrowed only until unlocking. */
static enum call_status check_rename(struct directory_object *source,
    struct directory_object *destination, uint64_t destination_rights,
    const struct directory_rename_request *request,
    struct directory_entry **old, struct directory_entry **replaced)
{
  *old = find_user_entry(source, request->source_name, request->source_length);
  *replaced = find_user_entry(destination, request->destination_name,
      request->destination_length);
  if (source->detached || destination->detached || !*old) {
    return CALL_NOT_FOUND;
  }
  if (entry_kind(*old) != DIRECTORY_KIND_FILE) {
    return CALL_WRONG_TYPE;
  }
  if (*old == *replaced) {
    return CALL_OK;
  }
  if (*replaced) {
    if (request->policy == DIRECTORY_RENAME_NO_REPLACE) {
      return CALL_ALREADY_EXISTS;
    }
    if (!(destination_rights & DIRECTORY_RIGHT_REMOVE)) {
      return CALL_DENIED;
    }
    if (entry_kind(*replaced) != DIRECTORY_KIND_FILE) {
      return CALL_WRONG_TYPE;
    }
  }
  if (source->generation == UINT64_MAX || destination->generation == UINT64_MAX ||
      (source != destination && !*replaced && destination->entry_count == SIZE_MAX)) {
    return CALL_LIMIT;
  }
  return CALL_OK;
}

static struct syscall_result rename_to_directory(struct directory_object *source,
    uint64_t rights, struct directory_object *destination, uint64_t destination_rights,
    const struct directory_rename_request *request)
{
  if (destination->backing == DIRECTORY_NPFS && (destination_rights & ~DIRECTORY_RIGHTS)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  bool native = source->backing == DIRECTORY_NPFS || destination->backing == DIRECTORY_NPFS;
  if (native && (source->backing != DIRECTORY_NPFS || destination->backing != DIRECTORY_NPFS)) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (native && (request->source_length > NPFS_NAME_MAX ||
      request->destination_length > NPFS_NAME_MAX)) {
    return (struct syscall_result){CALL_LIMIT, 0};
  }
  enum call_status status = check_name(request->source_name, request->source_length);
  if (status == CALL_OK) {
    status = check_name(request->destination_name, request->destination_length);
  }
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (native) {
    if (request->policy == DIRECTORY_RENAME_REPLACE &&
        !(destination_rights & DIRECTORY_RIGHT_REMOVE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    struct npfs_request *pending = npfs_request_prepare(NPFS_RENAME);
    pending->job.node = source->npfs;
    pending->job.destination = destination->npfs;
    pending->job.rights = rights;
    pending->job.destination_rights = destination_rights;
    pending->job.count = request->source_length;
    pending->job.destination_length = request->destination_length;
    pending->job.replace = request->policy == DIRECTORY_RENAME_REPLACE;
    KASSERT(copy_from_user(pending->job.name, request->source_name, request->source_length));
    pending->job.name[request->source_length] = 0;
    KASSERT(copy_from_user(pending->job.destination_name, request->destination_name,
        request->destination_length));
    pending->job.destination_name[request->destination_length] = 0;
    /* The caller retains both directories through worker completion. */
    npfs_request_submit_and_wait(pending);
    status = pending->job.status;
    npfs_request_release(pending);
    return (struct syscall_result){status, 0};
  }
  if (source->backing == DIRECTORY_HOST || destination->backing == DIRECTORY_HOST) {
    if (source->backing != DIRECTORY_HOST || destination->backing != DIRECTORY_HOST) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    /* The host may create a destination after any lookup. Replacement must
     * already be authorized when we submit the atomic name operation. */
    bool replace = request->policy == DIRECTORY_RENAME_REPLACE;
    if (replace && !(destination_rights & DIRECTORY_RIGHT_REMOVE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (request->source_length > VIRTIO_FS_NAME_MAX ||
        request->destination_length > VIRTIO_FS_NAME_MAX) {
      return (struct syscall_result){CALL_LIMIT, 0};
    }
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_RENAME);
    pending->node = source->host;
    pending->destination = destination->host;
    pending->count = request->source_length;
    pending->destination_length = request->destination_length;
    pending->replace = replace;
    KASSERT(copy_from_user(pending->name, request->source_name, request->source_length));
    KASSERT(copy_from_user(pending->destination_name, request->destination_name,
        request->destination_length));
    hostfs_request_submit_and_wait(pending);
    status = pending->status;
    hostfs_request_release(pending);
    return (struct syscall_result){status, 0};
  }
  if (source->backing != DIRECTORY_RAM || destination->backing != DIRECTORY_RAM) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }
  if (request->destination_length > SIZE_MAX - sizeof(struct directory_entry) - 1) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct directory_entry *old, *replaced;
  lock_parents(source, destination);
  status = check_rename(source, destination, destination_rights, request, &old, &replaced);
  bool unchanged = status == CALL_OK && old == replaced;
  unlock_parents(source, destination);
  if (status != CALL_OK || unchanged) {
    return (struct syscall_result){status, 0};
  }

  /* Owned directory references survive allocation; recheck both names. */
  struct directory_entry *entry = ramfs_request_name(request->destination_length);
  if (!entry) {
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  }
  KASSERT(copy_from_user(entry->name, request->destination_name, entry->name_length));
  entry->name[entry->name_length] = 0;

  lock_parents(source, destination);
  status = check_rename(source, destination, destination_rights, request, &old, &replaced);
  if (status == CALL_OK) {
    /* Transfer the source entry's reference, without copying file data or
     * changing existing handles. Nothing fallible remains before publication. */
    entry->object = old->object;
    unlink_entry(source, old);
    old->object = NULL;
    if (replaced) {
      unlink_entry(destination, replaced);
    }
    append_entry(destination, entry);
    ++source->generation;
    fs_metadata_touch(&source->metadata);
    if (destination != source) {
      ++destination->generation;
      fs_metadata_touch(&destination->metadata);
    }
  }
  unlock_parents(source, destination);

  if (status != CALL_OK) {
    ramfs_request_discard(entry);
  } else {
    ramfs_request_discard(old);
    if (replaced) {
      ramfs_request_discard(replaced);
    }
  }
  return (struct syscall_result){status, 0};
}

static struct syscall_result rename_child(struct directory_object *source, uint64_t rights,
    const struct directory_rename_request *request)
{
  if (request->policy != DIRECTORY_RENAME_NO_REPLACE &&
      request->policy != DIRECTORY_RENAME_REPLACE) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct capability_reference destination;
  enum capability_result result = capability_acquire(&process_current()->capabilities,
      request->destination, DIRECTORY_RIGHT_CREATE, 0, &destination);
  if (result != CAP_OK) {
    enum call_status status = result == CAP_LIMIT ? CALL_LIMIT :
        result == CAP_DENIED ? CALL_DENIED : CALL_BAD_HANDLE;
    return (struct syscall_result){status, 0};
  }
  struct syscall_result reply = {CALL_WRONG_TYPE, 0};
  if (destination.object->type == OBJECT_DIRECTORY) {
    reply = rename_to_directory(source, rights,
        (struct directory_object *)destination.object, destination.rights, request);
  }
  capability_release(&destination);
  return reply;
}

static struct syscall_result enumerate(struct directory_object *directory, uint64_t rights,
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
  if (directory->backing == DIRECTORY_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_ENUMERATE);
    pending->job.node = directory->npfs;
    pending->job.rights = rights;
    pending->job.cursor = request->cursor;
    pending->job.count = request->capacity;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    if (pending->job.entry.outcome == DIRECTORY_ENTRY) {
      KASSERT(pending->job.entry.name_size <= sizeof(pending->job.name) &&
          pending->job.entry.name_size <= request->capacity);
      KASSERT(copy_to_user(request->name, pending->job.name, pending->job.entry.name_size));
    }
    KASSERT(copy_to_user(reply_address, &pending->job.entry, sizeof(pending->job.entry)));
    npfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (directory->backing == DIRECTORY_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_ENUMERATE);
    pending->node = directory->host;
    pending->cursor = request->cursor;
    pending->count = request->capacity;
    hostfs_request_submit_and_wait(pending);
    enum call_status status = pending->status;
    if (status != CALL_OK) {
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    if (pending->entry.outcome == DIRECTORY_ENTRY) {
      KASSERT(copy_to_user(request->name, pending->name, pending->entry.name_size));
    }
    KASSERT(copy_to_user(reply_address, &pending->entry, sizeof(pending->entry)));
    hostfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply)};
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

static struct syscall_result filesystem_info(struct directory_object *directory, uint64_t rights,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (reply_capacity < sizeof(struct directory_filesystem_info)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct directory_filesystem_info), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct npfs_request *pending = npfs_request_prepare(NPFS_FILESYSTEM_INFO);
  pending->job.node = directory->npfs;
  pending->job.rights = rights;
  npfs_request_submit_and_wait(pending);
  enum call_status status = pending->job.status;
  if (status == CALL_OK) {
    KASSERT(copy_to_user(reply_address, &pending->job.info, sizeof(pending->job.info)));
  }
  npfs_request_release(pending);
  return (struct syscall_result){status, status == CALL_OK ? sizeof(struct directory_filesystem_info) : 0};
}

static struct syscall_result query_info(struct directory_object *directory, uint64_t rights,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct file_info_reply reply = {0};
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (directory->backing == DIRECTORY_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_INFO);
    pending->job.node = directory->npfs;
    pending->job.rights = rights;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status == CALL_OK) {
      KASSERT(copy_to_user(reply_address, &pending->job.file_info, sizeof(reply)));
    }
    npfs_request_release(pending);
    return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
  }
  if (directory->backing == DIRECTORY_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_INFO);
    pending->node = directory->host;
    hostfs_request_submit_and_wait(pending);
    enum call_status status = pending->status;
    if (status == CALL_OK) {
      KASSERT(copy_to_user(reply_address, &pending->info, sizeof(reply)));
    }
    hostfs_request_release(pending);
    return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
  }
  lock_directory(directory);
  fs_metadata_info(&directory->metadata, &reply);
  unlock_directory(directory);
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result directory_call(struct directory_object *directory, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (directory->backing == DIRECTORY_NPFS && (rights & ~DIRECTORY_RIGHTS)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t required;
  switch (operation) {
  case DIRECTORY_INFO:
    required = 0;
    break;
  case DIRECTORY_LOOKUP:
    required = DIRECTORY_RIGHT_LOOKUP;
    break;
  case DIRECTORY_ENUMERATE:
    required = DIRECTORY_RIGHT_ENUMERATE;
    break;
  case DIRECTORY_FILESYSTEM_INFO:
    if (directory->backing != DIRECTORY_NPFS) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    required = DIRECTORY_RIGHT_FILESYSTEM_INFO;
    break;
  case DIRECTORY_CREATE:
    required = DIRECTORY_RIGHT_CREATE;
    break;
  case DIRECTORY_SYNC:
    required = DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_REMOVE;
    break;
  case DIRECTORY_REMOVE:
  case DIRECTORY_RENAME:
    required = DIRECTORY_RIGHT_REMOVE;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (required && !(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  union directory_payload request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == DIRECTORY_INFO) {
    return query_info(directory, rights, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_FILESYSTEM_INFO) {
    return filesystem_info(directory, rights, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_LOOKUP) {
    return lookup(directory, rights, &request.lookup, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_CREATE) {
    return create_child(directory, rights, &request.create, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_REMOVE) {
    return remove_child(directory, rights, &request.remove);
  }
  if (operation == DIRECTORY_RENAME) {
    return rename_child(directory, rights, &request.rename);
  }
  if (operation == DIRECTORY_SYNC) {
    if (directory->backing == DIRECTORY_NPFS) {
      struct npfs_request *pending = npfs_request_prepare(NPFS_SYNC);
      pending->job.node = directory->npfs;
      pending->job.rights = rights;
      npfs_request_submit_and_wait(pending);
      enum call_status status = pending->job.status;
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    if (directory->backing == DIRECTORY_HOST) {
      struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_SYNC);
      pending->node = directory->host;
      hostfs_request_submit_and_wait(pending);
      enum call_status status = pending->status;
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    return (struct syscall_result){directory->backing == DIRECTORY_RAM ? CALL_OK : CALL_READ_ONLY, 0};
  }
  return enumerate(directory, rights, &request.enumerate, reply_address, reply_capacity);
}
