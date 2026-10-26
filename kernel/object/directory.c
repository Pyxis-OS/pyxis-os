#include <kernel/object/forwarder.h>
#include <abi/directory.h>
#include <abi/file.h>
#include <abi/package_commands.h>
#include <abi/package_sdk.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/fs/fat32.h>
#include <kernel/fs/ramfs.h>
#include <kernel/mm/heap.h>
#include <kernel/acpi/tables.h>
#include <kernel/completion.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

/* REMOVE may lock parent then child, while RENAME locks two arbitrary parents.
 * Serialize these multi-directory mutations before taking any directory lock.
 * RAM creation and parent retirement join the same topology boundary. Readers
 * only take their directory lock. No waits/allocations
 * may occur while it is held. */
#define RAM_RENAME_ANCESTORS_MAX 1024u

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
  if (directory->repository_parent) {
    object_release(&directory->repository_parent->object);
    kfree(directory);
    return;
  }
  if (directory->backing == DIRECTORY_FAT32) {
    fat32_retire_node(directory->fat32);
    return;
  }
  if (directory->backing == DIRECTORY_NPFS) {
    npfs_retire(directory->npfs);
    return;
  }
  if (directory->backing == DIRECTORY_HOST) {
    hostfs_retire(directory->host);
    return;
  }
  if (directory->backing == DIRECTORY_RAM) {
    lock_mutation();
    for (struct directory_entry *child = directory->first; child; child = child->next) {
      if (child->object->type == OBJECT_DIRECTORY) {
        ((struct directory_object *)child->object)->parent = NULL;
      }
    }
    unlock_mutation();
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
  KASSERT(backing == DIRECTORY_INITRD || backing == DIRECTORY_RAM || backing == DIRECTORY_HOST ||
      backing == DIRECTORY_ACPI);
  struct directory_object *directory = kmalloc(sizeof(*directory));
  if (!directory) {
    return NULL;
  }
  *directory = (struct directory_object){.backing = backing, .generation = 1};
  /* The ACPI root answers INFO like the archive: immutable identity, no entries. */
  if ((backing == DIRECTORY_RAM || backing == DIRECTORY_INITRD || backing == DIRECTORY_ACPI) &&
      !fs_metadata_create(&directory->metadata, backing != DIRECTORY_RAM)) {
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

void directory_init_fat32(struct directory_object *directory, struct fat32_node *node)
{
  KASSERT(arch_cpu_index() == 0);
  *directory = (struct directory_object){.backing = DIRECTORY_FAT32, .fat32 = node};
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
  if (directory->backing == DIRECTORY_RAM && entry->object->type == OBJECT_DIRECTORY) {
    ((struct directory_object *)entry->object)->parent = NULL;
  }
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
  if (directory->backing == DIRECTORY_RAM && entry->object->type == OBJECT_DIRECTORY) {
    ((struct directory_object *)entry->object)->parent = directory;
  }
  ++directory->entry_count;
}

static uint64_t entry_kind(const struct directory_entry *entry)
{
  return entry->object->type == OBJECT_DIRECTORY ? DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE;
}

static enum call_status check_name(uintptr_t address, size_t length)
{
  const char *name = (const void *)address;
  if (!length || (length == 1 && name[0] == '.') ||
      (length == 2 && name[0] == '.' && name[1] == '.')) {
    return CALL_BAD_REQUEST;
  }
  for (size_t i = 0; i < length; ++i) {
    if (!name[i] || name[i] == '/') {
      return CALL_BAD_REQUEST;
    }
  }
  return CALL_OK;
}

static enum call_status capture_name(uintptr_t address, size_t length, char **result)
{
  *result = NULL;
  if (!length) {
    return CALL_BAD_REQUEST;
  }
  if (!user_buffer_check(address, length, USER_BUFFER_READ)) {
    return CALL_BAD_BUFFER;
  }
  char *name = kmalloc(length);
  if (!name) {
    return CALL_NO_MEMORY;
  }
  if (!copy_from_user(name, address, length)) {
    kfree(name);
    return CALL_BAD_BUFFER;
  }
  enum call_status status = check_name((uintptr_t)name, length);
  if (status != CALL_OK) {
    kfree(name);
    return status;
  }
  *result = name;
  return CALL_OK;
}

static struct directory_entry *find_named_entry(struct directory_object *directory,
    uintptr_t name, size_t length)
{
  for (struct directory_entry *entry = directory->first; entry; entry = entry->next) {
    if (entry->name_length == length && !memcmp((const void *)name, entry->name, length)) {
      return entry;
    }
  }
  return NULL;
}

static bool repository_name_allowed(uintptr_t address, size_t length)
{
  const char *name = (const void *)address;
  return (length == sizeof("packages.lua") - 1 &&
          !memcmp(name, "packages.lua", length)) ||
      (length == sizeof("packages.lua.new") - 1 &&
       !memcmp(name, "packages.lua.new", length));
}

static enum call_status check_child_request(struct directory_object *directory, uint64_t rights,
    const struct directory_child_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  if (reply_capacity < sizeof(struct directory_child_reply) ||
      (request->kind != DIRECTORY_KIND_FILE && request->kind != DIRECTORY_KIND_DIRECTORY)) {
    return CALL_BAD_REQUEST;
  }
  if (directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) {
    uint64_t mask = request->kind == DIRECTORY_KIND_FILE ? FILE_RIGHTS : DIRECTORY_RIGHTS;
    if (request->rights & ~mask) {
      return CALL_BAD_REQUEST;
    }
    if (request->name_length > NPFS_NAME_MAX) {
      return CALL_NAME_TOO_LONG;
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
  return CALL_OK;
}

static enum call_status check_typed_lookup(uint64_t file_rights, uint64_t directory_rights,
    uintptr_t reply_address, size_t reply_capacity)
{
  if ((file_rights & ~FILE_RIGHTS) || (directory_rights & ~DIRECTORY_RIGHTS) ||
      reply_capacity < sizeof(struct directory_typed_lookup_reply)) {
    return CALL_BAD_REQUEST;
  }
  return user_buffer_check(reply_address, sizeof(struct directory_typed_lookup_reply),
      USER_BUFFER_WRITE) ? CALL_OK : CALL_BAD_BUFFER;
}

static uint64_t allowed_file_rights(uint64_t rights)
{
  return ((rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0) |
      ((rights & DIRECTORY_RIGHT_WRITE_FILES) ? FILE_RIGHT_WRITE : 0);
}

static enum call_status child_capability_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NOT_FOUND: return CALL_NOT_FOUND;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  default: panic("unexpected directory capability result %u", (unsigned)result);
  }
}

/* No directory lock or table-entry pointer survives the growth wait. */
static enum call_status reserve_child(struct capability_reservation *reservation,
    struct capability_reserved_slot *slot)
{
  enum call_status status = child_capability_status(
      capability_request_reservation(1, reservation, slot));
  if (status == CALL_OK && (task_stop_requested() || task_operation_denied())) {
    capability_reservation_release(reservation, slot);
    status = CALL_UNAVAILABLE;
  }
  return status;
}

static enum call_status prepare_child_grant(struct capability_reservation *reservation,
    struct kernel_object *object, uint64_t rights, struct capability_grant *grant)
{
  enum capability_result result = capability_grant_retain(object, rights, 0, grant);
  if (result == CAP_OK) {
    result = task_current_user() ? forwarder_request_derivation(grant) :
        forwarder_derive_grant(grant, task_forwarder_operation());
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(reservation->table, grant, 1);
  }
  if (result != CAP_OK) {
    capability_grant_release(grant);
  }
  return child_capability_status(result);
}

static enum call_status install_child(struct capability_reservation *reservation,
    const struct capability_reserved_slot *slot, struct kernel_object *object,
    uint64_t rights, handle_t *handle)
{
  struct capability_grant grant = {0};
  enum call_status status = prepare_child_grant(reservation, object, rights, &grant);
  if (status == CALL_OK) {
    capability_install_reserved(reservation, slot, &grant, 1, handle);
  }
  return status;
}

static bool package_body_zero(const void *bytes, size_t size)
{
  const uint8_t *body = bytes;
  for (size_t i = 0; i < size; ++i) if (body[i]) return false;
  return true;
}

static struct syscall_result package_commands_call(struct directory_object *directory,
    uint64_t rights, uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (directory->backing != DIRECTORY_NPFS) return (struct syscall_result){CALL_BAD_OPERATION, 0};
  bool query = operation == DIRECTORY_COMMAND_INFO;
  bool lookup = operation == DIRECTORY_COMMAND_LOOKUP;
  bool roots = operation == DIRECTORY_PACKAGE_ROOTS;
  bool select = operation == DIRECTORY_PACKAGE_ROOTS_SELECT;
  bool sdk = operation == DIRECTORY_PACKAGE_SDK;
  bool create = operation == DIRECTORY_PACKAGE_COMMANDS || roots || sdk;
  bool bind = operation == DIRECTORY_COMMAND_BIND;
  size_t reply_size = query ? sizeof(struct package_command_info) :
      lookup ? sizeof(struct package_command_lookup_reply) : sizeof(struct directory_child_reply);
  if (request_size != sizeof(union directory_payload) || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  union {
    struct package_command_capture_request capture;
    struct package_command_bind_request bind;
    struct package_command_lookup_request lookup;
    struct package_roots_select_request select;
    uint8_t bytes[sizeof(union directory_payload)];
  } input;
  if (!copy_from_user(&input, request_address, sizeof(input)) ||
      !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  uint64_t required = query ? 0 : DIRECTORY_RIGHT_LOOKUP;
  if (create || bind) required |= DIRECTORY_RIGHT_READ_FILES;
  if ((rights & required) != required) return (struct syscall_result){CALL_DENIED, 0};
  if ((create || query) ? !package_body_zero(&input, sizeof(input)) :
      lookup ? !package_body_zero(input.lookup.reserved, sizeof(input.lookup.reserved)) :
      select ? !package_body_zero(input.select.reserved, sizeof(input.select.reserved)) :
      !package_body_zero(input.capture.reserved, sizeof(input.capture.reserved))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (bind && !input.bind.capture) return (struct syscall_result){CALL_BAD_REQUEST, 0};
  char *name = NULL;
  enum call_status status = CALL_OK;
  struct capability_reference previous = {0};
  if (lookup) {
    uint64_t readonly = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | DIRECTORY_RIGHT_READ_FILES;
    if ((input.lookup.file_rights & ~FILE_RIGHT_READ) ||
        (input.lookup.directory_rights & ~readonly)) return (struct syscall_result){CALL_BAD_REQUEST, 0};
    if (input.lookup.name_length > PACKAGE_COMMAND_NAME_MAX) {
      return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
    }
    status = capture_name(input.lookup.name, input.lookup.name_length, &name);
  } else if (select) {
    if (input.select.name_length > PACKAGE_COMMAND_ID_MAX) {
      return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
    }
    status = capture_name(input.select.name, input.select.name_length, &name);
  }
  handle_t previous_handle = bind ? input.bind.capture : select ? input.select.previous :
      input.capture.previous;
  if (status == CALL_OK && !lookup && !create && !query && previous_handle) {
    enum capability_result result = capability_acquire(&process_current()->capabilities,
        previous_handle, DIRECTORY_RIGHT_LOOKUP, 0, &previous);
    if (result == CAP_OK) result = capability_reference_resolve(&previous);
    if (result != CAP_OK) {
      capability_release(&previous);
      if (name) kfree(name);
      return (struct syscall_result){result == CAP_BAD_HANDLE ? CALL_BAD_HANDLE :
          result == CAP_LIMIT ? CALL_LIMIT : result == CAP_NOT_FOUND ? CALL_NOT_FOUND : CALL_DENIED, 0};
    }
    if (previous.object->type != OBJECT_DIRECTORY) status = CALL_WRONG_TYPE;
    else if (((struct directory_object *)previous.object)->backing != DIRECTORY_NPFS) status = CALL_BAD_OPERATION;
    else if (((struct directory_object *)previous.object)->repository_parent) status = CALL_DENIED;
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (status == CALL_OK && !query) status = reserve_child(&reservation, &slot);
  struct package_command_lookup_reply reply = {0};
  struct kernel_object *object = NULL;
  uint64_t child_rights = 0;
  if (status == CALL_OK) {
    /* Resolve both handles before preparing the request: its operation carries
     * their combined CUT lineage, and both storage pins span worker completion. */
    enum npfs_operation code = roots ? NPFS_ROOT_VIEW : select ? NPFS_ROOT_SELECT :
        sdk ? NPFS_SDK_VIEW :
        create ? NPFS_COMMAND_VIEW :
        query ? NPFS_COMMAND_INFO :
        lookup ? NPFS_COMMAND_LOOKUP : bind ? NPFS_COMMAND_BIND : NPFS_COMMAND_CAPTURE;
    struct npfs_request *pending = npfs_request_prepare(code);
    pending->job.node = directory->npfs;
    pending->job.rights = rights;
    if (previous.object) {
      pending->job.destination = ((struct directory_object *)previous.object)->npfs;
      pending->job.destination_rights = previous.rights;
    }
    if (select) {
      pending->job.count = input.select.name_length;
      memcpy(pending->job.name, name, input.select.name_length);
      pending->job.name[input.select.name_length] = 0;
    }
    if (lookup) {
      pending->job.kind = DIRECTORY_KIND_ANY;
      pending->job.child_rights = input.lookup.file_rights;
      pending->job.directory_child_rights = input.lookup.directory_rights;
      pending->job.count = input.lookup.name_length;
      memcpy(pending->job.name, name, input.lookup.name_length);
      pending->job.name[input.lookup.name_length] = 0;
    }
    npfs_request_submit_and_wait(pending);
    status = pending->job.status;
    object = pending->job.object;
    pending->job.object = NULL;
    reply.info = pending->job.command_info;
    reply.target = pending->job.command_target;
    child_rights = pending->job.child_rights;
    npfs_request_release(pending);
  }
  if (status == CALL_OK && (task_stop_requested() || task_operation_denied())) status = CALL_UNAVAILABLE;
  if (status == CALL_OK && !query) {
    if (lookup) {
      reply.protocol = object_protocol(object);
      child_rights = object->type == OBJECT_DIRECTORY ? input.lookup.directory_rights : input.lookup.file_rights;
    }
    status = install_child(&reservation, &slot, object, child_rights, &reply.handle);
  }
  if (object) object_release(object);
  capability_release(&previous);
  capability_reservation_release(&reservation, &slot);
  if (name) kfree(name);
  if (status != CALL_OK) return (struct syscall_result){status, 0};
  bool copied = query ? copy_to_user(reply_address, &reply.info, sizeof(reply.info)) :
      copy_returned_handles_to_user(reply_address, &reply, reply_size, &reply.handle, 1);
  return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER, copied ? reply_size : 0};
}

static struct syscall_result repository_view(struct directory_object *parent,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (parent->backing != DIRECTORY_NPFS) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (reply_capacity < sizeof(struct directory_child_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct directory_child_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum call_status status = reserve_child(&reservation, &slot);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct directory_object *view = kmalloc(sizeof(*view));
  struct directory_object *original = parent->repository_parent ? parent->repository_parent : parent;
  if (!view) {
    status = CALL_NO_MEMORY;
  } else if (!object_retain(&original->object)) {
    kfree(view);
    view = NULL;
    status = CALL_LIMIT;
  } else {
    *view = (struct directory_object){
      .backing = DIRECTORY_NPFS,
      .npfs = original->npfs,
      .repository_parent = original,
    };
    object_init(&view->object, OBJECT_DIRECTORY, destroy_directory);
  }
  struct directory_child_reply reply = {0};
  if (status == CALL_OK) {
    status = install_child(&reservation, &slot, &view->object,
        DIRECTORY_REPOSITORY_VIEW_RIGHTS, &reply.handle);
  }
  if (view) {
    object_release(&view->object);
  }
  capability_reservation_release(&reservation, &slot);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

/* The ACPI worker answers by captured name and returns one owned sealed FILE.
 * A name longer than any table name cannot match. */
static enum call_status acpi_lookup(const struct directory_child_request *request,
    struct kernel_object **object)
{
  *object = NULL;
  if (request->name_length > ACPI_TABLE_NAME_MAX) {
    return CALL_NOT_FOUND;
  }
  struct acpi_tables_request *pending = (void *)bsp_request_prepare(BSP_SERVICE_ACPI_TABLES);
  pending->operation = ACPI_TABLES_OPEN;
  pending->kind = request->kind;
  memcpy(pending->name, (const void *)request->name, request->name_length);
  pending->name[request->name_length] = 0;
  bsp_request_submit_and_wait(&pending->request);
  enum call_status status = pending->status;
  if (pending->file) {
    *object = &pending->file->object;
    pending->file = NULL;
  }
  bsp_request_release(&pending->request);
  return status;
}

static struct syscall_result lookup(struct directory_object *directory, uint64_t rights,
    const struct directory_child_request *request, uint64_t directory_rights, uintptr_t reply_address,
    size_t reply_capacity)
{
  bool typed = request->kind == DIRECTORY_KIND_ANY;
  enum call_status status = typed ?
      check_typed_lookup(request->rights, directory_rights, reply_address, reply_capacity) :
      check_child_request(directory, rights, request, reply_address, reply_capacity);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }

  if (directory->backing == DIRECTORY_HOST && request->name_length > VIRTIO_FS_NAME_MAX) {
    return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  status = reserve_child(&reservation, &slot);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct kernel_object *object = NULL;
  if (directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_LOOKUP);
    pending->job.node = directory->npfs;
    pending->job.fat_node = directory->fat32;
    if (directory->backing == DIRECTORY_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    pending->job.child_rights = request->rights;
    pending->job.directory_child_rights = directory_rights;
    /* Typed lookup through the view still returns its typed reply, but the
     * worker must only resolve a regular file, never a directory child. */
    pending->job.kind = directory->repository_parent ? DIRECTORY_KIND_FILE : request->kind;
    pending->job.count = request->name_length;
    memcpy(pending->job.name, (const void *)request->name, request->name_length);
    pending->job.name[request->name_length] = 0;
    npfs_request_submit_and_wait(pending);
    status = pending->job.status;
    object = pending->job.object;
    pending->job.object = NULL;
    npfs_request_release(pending);
  } else if (directory->backing == DIRECTORY_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_LOOKUP);
    pending->node = directory->host;
    pending->kind = request->kind;
    pending->lookup_rights = rights;
    pending->file_rights = request->rights;
    pending->directory_rights = directory_rights;
    pending->count = request->name_length;
    memcpy(pending->name, (const void *)request->name, request->name_length);
    hostfs_request_submit_and_wait(pending);
    status = pending->status;
    object = pending->object;
    pending->object = NULL;
    hostfs_request_release(pending);
  } else if (directory->backing == DIRECTORY_ACPI) {
    status = acpi_lookup(request, &object);
  } else {
    lock_directory(directory);
    struct directory_entry *entry = find_named_entry(directory, request->name, request->name_length);
    if (!entry) {
      status = CALL_NOT_FOUND;
    } else if (typed && entry->object->type != OBJECT_FILE &&
        entry->object->type != OBJECT_DIRECTORY) {
      status = CALL_BAD_OPERATION;
    } else if (!typed && entry_kind(entry) != request->kind) {
      status = CALL_WRONG_TYPE;
    } else if (!object_retain(entry->object)) {
      status = CALL_LIMIT;
    } else {
      object = entry->object;
    }
    unlock_directory(directory);
  }
  if (status == CALL_OK && (task_stop_requested() || task_operation_denied())) {
    status = CALL_UNAVAILABLE;
  }
  if (status != CALL_OK) {
    if (object) {
      object_release(object);
    }
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){status, 0};
  }

  uint64_t child_rights = typed && object->type == OBJECT_DIRECTORY ?
      directory_rights : request->rights;
  uint64_t allowed = object->type == OBJECT_DIRECTORY ? rights : allowed_file_rights(rights);
  if (directory->repository_parent && object->type != OBJECT_FILE) {
    status = CALL_DENIED;
  } else if (typed && (object->type != OBJECT_FILE && object->type != OBJECT_DIRECTORY)) {
    status = CALL_BAD_OPERATION;
  } else if (typed && (child_rights & ~allowed)) {
    status = CALL_DENIED;
  }
  struct directory_typed_lookup_reply reply = {.protocol = object_protocol(object)};
  if (status == CALL_OK) {
    status = install_child(&reservation, &slot, object, child_rights, &reply.handle);
  }
  object_release(object);
  capability_reservation_release(&reservation, &slot);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  size_t reply_size = typed ? sizeof(reply) : sizeof(struct directory_child_reply);
  if (!copy_returned_handles_to_user(reply_address, &reply, reply_size, &reply.handle, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, reply_size};
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
  if (directory->backing == DIRECTORY_HOST && request->name_length > VIRTIO_FS_NAME_MAX) {
    return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
  }
  if (directory->backing != DIRECTORY_NPFS && directory->backing != DIRECTORY_FAT32 &&
      directory->backing != DIRECTORY_HOST &&
      directory->backing != DIRECTORY_RAM) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }
  if (directory->backing == DIRECTORY_RAM &&
      request->name_length > SIZE_MAX - sizeof(struct directory_entry) - 1) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  status = reserve_child(&reservation, &slot);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_CREATE);
    pending->job.node = directory->npfs;
    pending->job.fat_node = directory->fat32;
    if (directory->backing == DIRECTORY_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    pending->job.child_rights = request->rights;
    pending->job.kind = request->kind;
    pending->job.count = request->name_length;
    pending->job.reservation = reservation;
    pending->job.slot = slot;
    reservation = (struct capability_reservation){0};
    memcpy(pending->job.name, (const void *)request->name, request->name_length);
    pending->job.name[request->name_length] = 0;
    npfs_request_submit_and_wait(pending);
    status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    struct directory_child_reply reply = {.handle = pending->job.handle};
    bool copied = copy_returned_handles_to_user(reply_address, &reply, sizeof(reply),
        &reply.handle, 1);
    npfs_request_release(pending);
    return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER, copied ? sizeof(reply) : 0};
  }
  if (directory->backing == DIRECTORY_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_CREATE);
    pending->node = directory->host;
    pending->kind = request->kind;
    pending->count = request->name_length;
    pending->reservation = reservation;
    pending->slot = slot;
    reservation = (struct capability_reservation){0};
    pending->rights = request->rights;
    memcpy(pending->name, (const void *)request->name, request->name_length);
    hostfs_request_submit_and_wait(pending);
    status = pending->status;
    if (status != CALL_OK) {
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    struct directory_child_reply reply = {.handle = pending->handle};
    bool copied = copy_returned_handles_to_user(reply_address, &reply, sizeof(reply),
        &reply.handle, 1);
    hostfs_request_release(pending);
    return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER, copied ? sizeof(reply) : 0};
  }

  lock_directory(directory);
  bool detached = directory->detached;
  bool exists = find_named_entry(directory, request->name, request->name_length) != NULL;
  unlock_directory(directory);
  if (detached || exists) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){detached ? CALL_NOT_FOUND : CALL_ALREADY_EXISTS, 0};
  }

  struct directory_entry *entry = ramfs_request_entry(request->kind, request->name_length);
  if (!entry) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  }
  /* The immutable capture survives the BSP's unpublished entry allocation. */
  memcpy(entry->name, (const void *)request->name, request->name_length);
  entry->name[entry->name_length] = 0;
  struct capability_grant grant = {0};
  status = prepare_child_grant(&reservation, entry->object, request->rights, &grant);
  if (status != CALL_OK || (task_stop_requested() || task_operation_denied())) {
    capability_grant_release(&grant);
    capability_reservation_release(&reservation, &slot);
    ramfs_request_discard(entry);
    return (struct syscall_result){status != CALL_OK ? status : CALL_UNAVAILABLE, 0};
  }

  struct directory_child_reply reply;
  lock_mutation();
  lock_directory(directory);
  /* Recheck after the allocation wait. Namespace publication and the owned
   * grant move cannot fail once these checks pass. */
  if (directory->detached) {
    status = CALL_NOT_FOUND;
  } else if (name_exists(directory, entry)) {
    status = CALL_ALREADY_EXISTS;
  } else if (directory->generation == UINT64_MAX || directory->entry_count == SIZE_MAX) {
    status = CALL_LIMIT;
  } else if (!task_operation_start()) {
    status = CALL_DENIED;
  } else {
    append_entry(directory, entry);
    ++directory->generation;
    fs_metadata_touch(&directory->metadata);
    capability_install_reserved(&reservation, &slot, &grant, 1, &reply.handle);
  }
  unlock_directory(directory);
  unlock_mutation();

  capability_grant_release(&grant);
  capability_reservation_release(&reservation, &slot);
  if (status != CALL_OK) {
    ramfs_request_discard(entry);
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static enum call_status unlink_ram_child(struct directory_object *directory,
    const char *name, size_t length, uint64_t kind, struct directory_entry **removed)
{
  enum call_status status = CALL_OK;
  *removed = NULL;
  lock_mutation();
  lock_directory(directory);
  struct directory_entry *entry = find_named_entry(directory, (uintptr_t)name, length);
  if (!entry) {
    status = CALL_NOT_FOUND;
  } else if (kind != DIRECTORY_KIND_ANY && entry_kind(entry) != kind) {
    status = CALL_WRONG_TYPE;
  } else if (directory->generation == UINT64_MAX) {
    status = CALL_LIMIT;
  } else if (!task_operation_start()) {
    status = CALL_DENIED;
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
    *removed = entry;
  }
  return status;
}

static struct syscall_result remove_child(struct directory_object *directory, uint64_t rights,
    const struct directory_remove_request *request)
{
  if (request->kind != DIRECTORY_KIND_ANY && request->kind != DIRECTORY_KIND_FILE &&
      request->kind != DIRECTORY_KIND_DIRECTORY && request->kind != DIRECTORY_KIND_SYMLINK) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (request->kind == DIRECTORY_KIND_SYMLINK && directory->backing != DIRECTORY_HOST) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if ((directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) &&
      request->name_length > NPFS_NAME_MAX) {
    return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
  }
  enum call_status status = check_name(request->name, request->name_length);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_REMOVE);
    pending->job.node = directory->npfs;
    pending->job.fat_node = directory->fat32;
    if (directory->backing == DIRECTORY_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    pending->job.kind = request->kind;
    pending->job.count = request->name_length;
    memcpy(pending->job.name, (const void *)request->name, request->name_length);
    pending->job.name[request->name_length] = 0;
    npfs_request_submit_and_wait(pending);
    status = pending->job.status;
    npfs_request_release(pending);
    return (struct syscall_result){status, 0};
  }
  if (directory->backing == DIRECTORY_HOST) {
    if (request->name_length > VIRTIO_FS_NAME_MAX) {
      return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
    }
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_REMOVE);
    pending->node = directory->host;
    pending->kind = request->kind;
    pending->count = request->name_length;
    memcpy(pending->name, (const void *)request->name, request->name_length);
    hostfs_request_submit_and_wait(pending);
    status = pending->status;
    hostfs_request_release(pending);
    return (struct syscall_result){status, 0};
  }
  if (directory->backing != DIRECTORY_RAM) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }

  struct directory_entry *removed;
  status = unlink_ram_child(directory, (const char *)request->name,
      request->name_length, request->kind, &removed);
  if (removed) {
    /* The removed entry still owns its child until BSP disposal. Independent
     * handles keep the object alive after that reference is released. */
    ramfs_request_discard(removed);
  }
  return (struct syscall_result){status, 0};
}

enum call_status directory_completion_reserve(struct native_completion *operation)
{
  struct directory_object *directory = (struct directory_object *)operation->target.object;
  uint64_t kind, length, child_rights = 0;
  size_t header_size;
  enum npfs_operation code;
  uint64_t required;
  if (operation->operation == DIRECTORY_CREATE) {
    struct completion_create_request request;
    if (operation->input_size < sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, operation->input, sizeof(request));
    kind = request.kind;
    length = request.name_length;
    child_rights = request.rights;
    header_size = sizeof(request);
    code = NPFS_CREATE;
    required = DIRECTORY_RIGHT_CREATE;
    if (kind != DIRECTORY_KIND_FILE || child_rights != FILE_RIGHTS) {
      return CALL_BAD_REQUEST;
    }
    if (!(operation->target.rights & DIRECTORY_RIGHT_READ_FILES) ||
        !(operation->target.rights & DIRECTORY_RIGHT_WRITE_FILES)) {
      return CALL_DENIED;
    }
  } else if (operation->operation == DIRECTORY_REMOVE) {
    struct completion_remove_request request;
    if (operation->input_size < sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, operation->input, sizeof(request));
    kind = request.kind;
    length = request.name_length;
    header_size = sizeof(request);
    code = NPFS_REMOVE;
    required = DIRECTORY_RIGHT_REMOVE;
    if (kind != DIRECTORY_KIND_FILE && kind != DIRECTORY_KIND_DIRECTORY &&
        kind != DIRECTORY_KIND_ANY) {
      return CALL_BAD_REQUEST;
    }
  } else {
    return CALL_BAD_OPERATION;
  }
  if (!(operation->target.rights & required)) {
    return CALL_DENIED;
  }
  if (operation->input_size > COMPLETION_PAYLOAD_MAX ||
      length != operation->input_size - header_size) {
    return CALL_BAD_REQUEST;
  }
  if (directory->backing != DIRECTORY_RAM && directory->backing != DIRECTORY_INITRD &&
      directory->backing != DIRECTORY_NPFS) {
    return CALL_BAD_OPERATION;
  }
  if (directory->backing == DIRECTORY_NPFS && length > NPFS_NAME_MAX) {
    return CALL_NAME_TOO_LONG;
  }
  const char *name = (const char *)operation->input + header_size;
  enum call_status status = check_name((uintptr_t)name, length);
  if (status != CALL_OK) {
    return status;
  }
  if (directory->repository_parent) {
    if (!repository_name_allowed((uintptr_t)name, length) || kind == DIRECTORY_KIND_DIRECTORY) {
      return CALL_DENIED;
    }
    kind = DIRECTORY_KIND_FILE;
  }
  if (directory->backing == DIRECTORY_NPFS) {
    status = npfs_completion_reserve(operation, code, directory->npfs);
    if (status == CALL_OK) {
      struct npfs_job *job = operation->backend;
      job->kind = kind;
      job->child_rights = child_rights;
      job->count = length;
      memcpy(job->name, name, length);
      job->name[length] = 0;
    }
  }
  return status;
}

void directory_completion_discard(struct native_completion *operation)
{
  struct directory_object *directory = (struct directory_object *)operation->target.object;
  if (directory->backing == DIRECTORY_NPFS) {
    npfs_completion_discard(operation);
  }
}

static enum call_status completion_create_ram(struct directory_object *directory,
    const char *name, size_t length, struct capability_grant *grant)
{
  lock_directory(directory);
  bool detached = directory->detached;
  bool exists = find_named_entry(directory, (uintptr_t)name, length) != NULL;
  unlock_directory(directory);
  if (detached || exists) {
    return detached ? CALL_NOT_FOUND : CALL_ALREADY_EXISTS;
  }
  struct directory_entry *entry = kmalloc(sizeof(*entry) + length + 1);
  if (!entry) {
    return CALL_NO_MEMORY;
  }
  *entry = (struct directory_entry){.name_length = length};
  memcpy(entry->name, name, length);
  entry->name[length] = 0;
  struct file_object *file = file_create_ram();
  if (!file) {
    kfree(entry);
    return CALL_NO_MEMORY;
  }
  entry->object = &file->object;
  enum capability_result retained = capability_grant_retain(entry->object, FILE_RIGHTS, 0, grant);
  if (retained == CAP_OK) {
    retained = forwarder_derive_grant(grant, task_forwarder_operation());
  }
  enum call_status status = child_capability_status(retained);
  if (status == CALL_OK) {
    lock_mutation();
    lock_directory(directory);
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
    unlock_mutation();
  }
  if (status != CALL_OK) {
    capability_grant_release(grant);
    object_release(entry->object);
    kfree(entry);
  }
  return status;
}

static enum call_status completion_remove_ram(struct directory_object *directory,
    const char *name, size_t length, uint64_t kind)
{
  struct directory_entry *removed;
  enum call_status status = unlink_ram_child(directory, name, length, kind, &removed);
  if (removed) {
    object_release(removed->object);
    kfree(removed);
  }
  return status;
}

void directory_completion_start(struct native_completion *operation)
{
  struct directory_object *directory = (struct directory_object *)operation->target.object;
  if (directory->backing == DIRECTORY_NPFS) {
    npfs_completion_start(operation);
    return;
  }
  enum call_status status;
  struct capability_grant grant = {0};
  if (!native_completion_mark_started(operation)) {
    status = native_completion_cancel_reason(operation);
  } else if (directory->backing != DIRECTORY_RAM) {
    status = CALL_READ_ONLY;
  } else if (operation->operation == DIRECTORY_CREATE) {
    struct completion_create_request request;
    memcpy(&request, operation->input, sizeof(request));
    status = completion_create_ram(directory,
        (const char *)operation->input + sizeof(request), request.name_length, &grant);
  } else {
    KASSERT(operation->operation == DIRECTORY_REMOVE);
    struct completion_remove_request request;
    memcpy(&request, operation->input, sizeof(request));
    status = completion_remove_ram(directory,
        (const char *)operation->input + sizeof(request), request.name_length, request.kind);
  }
  native_completion_finish(operation, status, NULL, 0, &grant);
}

/* Mutation lock keeps borrowed parent links alive and excludes topology edits. */
static enum call_status check_directory_destination(struct directory_object *moved,
    struct directory_object *destination)
{
  for (unsigned depth = 0; destination; ++depth) {
    if (destination == moved) {
      return CALL_BAD_REQUEST;
    }
    if (depth == RAM_RENAME_ANCESTORS_MAX) {
      return CALL_LIMIT;
    }
    destination = destination->parent;
  }
  return CALL_OK;
}

/* Both parents locked. Returned entries are borrowed only until unlocking. */
static enum call_status check_rename(struct directory_object *source,
    struct directory_object *destination, uint64_t destination_rights,
    const struct directory_rename_request *request,
    struct directory_entry **old, struct directory_entry **replaced)
{
  *old = find_named_entry(source, request->source_name, request->source_length);
  *replaced = find_named_entry(destination, request->destination_name,
      request->destination_length);
  if (source->detached || destination->detached || !*old) {
    return CALL_NOT_FOUND;
  }
  uint64_t kind = entry_kind(*old);
  if (request->kind != DIRECTORY_KIND_ANY && request->kind != kind) {
    return CALL_WRONG_TYPE;
  }
  if (*old == *replaced) {
    return CALL_OK;
  }
  if (kind == DIRECTORY_KIND_DIRECTORY) {
    enum call_status status = check_directory_destination((void *)(*old)->object, destination);
    if (status != CALL_OK) {
      return status;
    }
  }
  if (*replaced) {
    if (request->policy == DIRECTORY_RENAME_NO_REPLACE) {
      return CALL_ALREADY_EXISTS;
    }
    if (!(destination_rights & DIRECTORY_RIGHT_REMOVE)) {
      return CALL_DENIED;
    }
    if (entry_kind(*replaced) != kind) {
      return CALL_WRONG_TYPE;
    }
    if (kind == DIRECTORY_KIND_DIRECTORY) {
      struct directory_object *victim = (void *)(*replaced)->object;
      bool parent_locked = victim == source || victim == destination;
      if (!parent_locked) {
        lock_directory(victim);
      }
      bool empty = victim->entry_count == 0;
      if (!parent_locked) {
        unlock_directory(victim);
      }
      if (!empty) {
        return CALL_NOT_EMPTY;
      }
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
  if ((destination->backing == DIRECTORY_NPFS || destination->backing == DIRECTORY_FAT32) &&
      (destination_rights & ~DIRECTORY_RIGHTS)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  bool native = source->backing == DIRECTORY_NPFS || source->backing == DIRECTORY_FAT32 ||
      destination->backing == DIRECTORY_NPFS || destination->backing == DIRECTORY_FAT32;
  if (native && source->backing != destination->backing) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (native && (request->source_length > NPFS_NAME_MAX ||
      request->destination_length > NPFS_NAME_MAX)) {
    return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
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
    pending->job.fat_node = source->fat32;
    pending->job.fat_destination = destination->fat32;
    if (source->backing == DIRECTORY_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    pending->job.destination_rights = destination_rights;
    pending->job.count = request->source_length;
    pending->job.destination_length = request->destination_length;
    pending->job.replace = request->policy == DIRECTORY_RENAME_REPLACE;
    pending->job.kind = request->kind;
    memcpy(pending->job.name, (const void *)request->source_name, request->source_length);
    pending->job.name[request->source_length] = 0;
    memcpy(pending->job.destination_name, (const void *)request->destination_name, request->destination_length);
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
      return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
    }
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_RENAME);
    pending->node = source->host;
    pending->destination = destination->host;
    pending->count = request->source_length;
    pending->destination_length = request->destination_length;
    pending->replace = replace;
    pending->kind = request->kind;
    memcpy(pending->name, (const void *)request->source_name, request->source_length);
    memcpy(pending->destination_name, (const void *)request->destination_name, request->destination_length);
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
  memcpy(entry->name, (const void *)request->destination_name, entry->name_length);
  entry->name[entry->name_length] = 0;

  lock_parents(source, destination);
  status = check_rename(source, destination, destination_rights, request, &old, &replaced);
  unchanged = status == CALL_OK && old == replaced;
  if (status == CALL_OK && !task_operation_start()) {
    status = CALL_DENIED;
  }
  if (status == CALL_OK && !unchanged) {
    /* Transfer the source entry's reference, without copying file data or
     * changing existing handles. Nothing fallible remains before publication. */
    entry->object = old->object;
    unlink_entry(source, old);
    old->object = NULL;
    if (replaced) {
      if (entry_kind(replaced) == DIRECTORY_KIND_DIRECTORY) {
        struct directory_object *victim = (void *)replaced->object;
        lock_directory(victim);
        victim->detached = true;
        unlock_directory(victim);
      }
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

  if (status != CALL_OK || unchanged) {
    ramfs_request_discard(entry);
  } else {
    ramfs_request_discard(old);
    if (replaced) {
      ramfs_request_discard(replaced);
    }
  }
  return (struct syscall_result){status, 0};
}

static struct syscall_result link_child(struct directory_object *source, uint64_t rights,
    const struct directory_link_request *request)
{
  if (source->backing != DIRECTORY_NPFS) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (request->source_length > NPFS_NAME_MAX || request->destination_length > NPFS_NAME_MAX) {
    return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
  }
  struct capability_reference destination = {0};
  enum capability_result result = capability_acquire(&process_current()->capabilities,
      request->destination, DIRECTORY_RIGHT_CREATE, 0, &destination);
  if (result == CAP_OK) {
    result = capability_reference_resolve(&destination);
  }
  if (result != CAP_OK) {
    capability_release(&destination);
    enum call_status status = result == CAP_LIMIT ? CALL_LIMIT :
        result == CAP_NOT_FOUND ? CALL_NOT_FOUND :
        result == CAP_DENIED ? CALL_DENIED : CALL_BAD_HANDLE;
    return (struct syscall_result){status, 0};
  }
  enum call_status status = CALL_WRONG_TYPE;
  char *source_name = NULL, *destination_name = NULL;
  if (destination.object->type == OBJECT_DIRECTORY) {
    struct directory_object *target = (void *)destination.object;
    status = target->repository_parent ? CALL_DENIED :
        target->backing != DIRECTORY_NPFS ? CALL_BAD_OPERATION :
        !(destination.rights & DIRECTORY_RIGHT_CREATE) ? CALL_DENIED : CALL_OK;
    if (status == CALL_OK) {
      status = capture_name(request->source_name, request->source_length, &source_name);
    }
    if (status == CALL_OK) {
      status = capture_name(request->destination_name, request->destination_length, &destination_name);
    }
    if (status == CALL_OK) {
      /* Resolution precedes request preparation so both parents' lineages are
       * captured. Retain both objects through the owning worker's completion. */
      struct npfs_request *pending = npfs_request_prepare(NPFS_LINK);
      pending->job.node = source->npfs;
      pending->job.destination = target->npfs;
      pending->job.rights = rights;
      pending->job.destination_rights = destination.rights;
      pending->job.count = request->source_length;
      pending->job.destination_length = request->destination_length;
      memcpy(pending->job.name, source_name, request->source_length);
      pending->job.name[request->source_length] = 0;
      memcpy(pending->job.destination_name, destination_name, request->destination_length);
      pending->job.destination_name[request->destination_length] = 0;
      npfs_request_submit_and_wait(pending);
      status = pending->job.status;
      npfs_request_release(pending);
    }
  }
  if (source_name) {
    kfree(source_name);
  }
  if (destination_name) {
    kfree(destination_name);
  }
  capability_release(&destination);
  return (struct syscall_result){status, 0};
}

static struct syscall_result rename_child(struct directory_object *source, uint64_t rights,
    const struct directory_rename_request *request)
{
  if ((request->policy != DIRECTORY_RENAME_NO_REPLACE &&
      request->policy != DIRECTORY_RENAME_REPLACE) ||
      (request->kind != DIRECTORY_KIND_ANY && request->kind != DIRECTORY_KIND_FILE &&
      request->kind != DIRECTORY_KIND_DIRECTORY)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct capability_reference destination;
  enum capability_result result = capability_acquire(&process_current()->capabilities,
      request->destination, DIRECTORY_RIGHT_CREATE, 0, &destination);
  if (result == CAP_OK) {
    result = capability_reference_resolve(&destination);
  }
  if (result != CAP_OK) {
    capability_release(&destination);
    enum call_status status = result == CAP_LIMIT ? CALL_LIMIT :
        result == CAP_NOT_FOUND ? CALL_NOT_FOUND :
        result == CAP_DENIED ? CALL_DENIED : CALL_BAD_HANDLE;
    return (struct syscall_result){status, 0};
  }
  struct syscall_result reply = {CALL_WRONG_TYPE, 0};
  char *source_name = NULL, *destination_name = NULL;
  if (destination.object->type == OBJECT_DIRECTORY) {
    struct directory_object *target = (void *)destination.object;
    bool native = source->backing == DIRECTORY_NPFS || source->backing == DIRECTORY_FAT32 ||
        target->backing == DIRECTORY_NPFS || target->backing == DIRECTORY_FAT32;
    bool host = source->backing == DIRECTORY_HOST || target->backing == DIRECTORY_HOST;
    enum call_status status = CALL_OK;
    if ((native && source->backing != target->backing) ||
        (host && (source->backing != DIRECTORY_HOST || target->backing != DIRECTORY_HOST))) {
      status = CALL_BAD_OPERATION;
    } else if ((native && (request->source_length > NPFS_NAME_MAX ||
        request->destination_length > NPFS_NAME_MAX)) ||
        (host && (request->source_length > VIRTIO_FS_NAME_MAX ||
        request->destination_length > VIRTIO_FS_NAME_MAX))) {
      status = CALL_NAME_TOO_LONG;
    } else if (!native && !host &&
        (source->backing != DIRECTORY_RAM || target->backing != DIRECTORY_RAM)) {
      status = CALL_READ_ONLY;
    } else if (request->policy == DIRECTORY_RENAME_REPLACE &&
        !(destination.rights & DIRECTORY_RIGHT_REMOVE)) {
      status = CALL_DENIED;
    }
    struct directory_rename_request captured = *request;
    if (status == CALL_OK) {
      status = capture_name(request->source_name, request->source_length, &source_name);
    }
    if (status == CALL_OK) {
      status = capture_name(request->destination_name, request->destination_length, &destination_name);
    }
    if (status == CALL_OK && (source->repository_parent || target->repository_parent)) {
      if (!source->repository_parent || !target->repository_parent || source->npfs != target->npfs ||
          request->kind == DIRECTORY_KIND_DIRECTORY ||
          !repository_name_allowed((uintptr_t)source_name, request->source_length) ||
          !repository_name_allowed((uintptr_t)destination_name, request->destination_length)) {
        status = CALL_DENIED;
      } else {
        captured.kind = DIRECTORY_KIND_FILE;
      }
    }
    if (status == CALL_OK) {
      captured.source_name = (uintptr_t)source_name;
      captured.destination_name = (uintptr_t)destination_name;
      reply = rename_to_directory(source, rights, target, destination.rights, &captured);
    } else {
      reply = (struct syscall_result){status, 0};
    }
  }
  if (source_name) {
    kfree(source_name);
  }
  if (destination_name) {
    kfree(destination_name);
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
  if (directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_ENUMERATE);
    pending->job.node = directory->npfs;
    pending->job.fat_node = directory->fat32;
    if (directory->backing == DIRECTORY_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    pending->job.cursor = request->cursor;
    pending->job.count = request->capacity;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    size_t name_size = pending->job.entry.outcome == DIRECTORY_ENTRY ?
        pending->job.entry.name_size : 0;
    KASSERT(name_size <= sizeof(pending->job.name) && name_size <= request->capacity);
    bool copied = copy_reply_to_user(reply_address, &pending->job.entry,
        sizeof(pending->job.entry), request->name, pending->job.name, name_size);
    npfs_request_release(pending);
    return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER, copied ? sizeof(reply) : 0};
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
    size_t name_size = pending->entry.outcome == DIRECTORY_ENTRY ? pending->entry.name_size : 0;
    bool copied = copy_reply_to_user(reply_address, &pending->entry, sizeof(pending->entry),
        request->name, pending->name, name_size);
    hostfs_request_release(pending);
    return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER, copied ? sizeof(reply) : 0};
  }
  if (!request->cursor.generation && request->cursor.position) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (directory->backing == DIRECTORY_ACPI) {
    struct acpi_tables_request *pending = (void *)bsp_request_prepare(BSP_SERVICE_ACPI_TABLES);
    pending->operation = ACPI_TABLES_ENUMERATE;
    pending->cursor = request->cursor;
    pending->capacity = request->capacity;
    bsp_request_submit_and_wait(&pending->request);
    enum call_status status = pending->status;
    if (status != CALL_OK) {
      bsp_request_release(&pending->request);
      return (struct syscall_result){status, 0};
    }
    size_t name_size = pending->entry.outcome == DIRECTORY_ENTRY ? pending->entry.name_size : 0;
    KASSERT(name_size <= sizeof(pending->name) && name_size <= request->capacity);
    bool copied = copy_reply_to_user(reply_address, &pending->entry, sizeof(pending->entry),
        request->name, pending->name, name_size);
    bsp_request_release(&pending->request);
    return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER, copied ? sizeof(reply) : 0};
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
  /* The directory owns the name through this nonblocking leased copy. */
  size_t name_size = reply.outcome == DIRECTORY_ENTRY ? reply.name_size : 0;
  bool copied = copy_reply_to_user(reply_address, &reply, sizeof(reply), request->name,
      name_size ? entry->name : NULL, name_size);
  unlock_directory(directory);
  return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER, copied ? sizeof(reply) : 0};
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
  if (directory->backing == DIRECTORY_RAM) {
    struct directory_filesystem_info reply = {.type = FILESYSTEM_TYPE_RAM};
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  struct npfs_request *pending = npfs_request_prepare(NPFS_FILESYSTEM_INFO);
  pending->job.node = directory->npfs;
  pending->job.rights = rights;
  npfs_request_submit_and_wait(pending);
  enum call_status status = pending->job.status;
  if (status == CALL_OK) {
    if (!copy_to_user(reply_address, &pending->job.info, sizeof(pending->job.info))) {
      status = CALL_BAD_BUFFER;
    }
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
  if (directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_INFO);
    pending->job.node = directory->npfs;
    pending->job.fat_node = directory->fat32;
    if (directory->backing == DIRECTORY_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status == CALL_OK) {
      if (!copy_to_user(reply_address, &pending->job.file_info, sizeof(reply))) {
        status = CALL_BAD_BUFFER;
      }
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
      if (!copy_to_user(reply_address, &pending->info, sizeof(reply))) {
        status = CALL_BAD_BUFFER;
      }
    }
    hostfs_request_release(pending);
    return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
  }
  lock_directory(directory);
  fs_metadata_info(&directory->metadata, &reply);
  unlock_directory(directory);
  bool copied = copy_to_user(reply_address, &reply, sizeof(reply));
  return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER, copied ? sizeof(reply) : 0};
}

/* ACPI_TABLES_INFO: the directory message body carries an
 * acpi_table_info_request. The name is captured like a lookup name. */
static struct syscall_result acpi_table_query(const union directory_payload *body,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct acpi_table_info_request query;
  memcpy(&query, body, sizeof(query));
  if (reply_capacity < sizeof(struct acpi_table_info)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct acpi_table_info), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  char *name;
  enum call_status status = capture_name(query.name, query.name_length, &name);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (query.name_length > ACPI_TABLE_NAME_MAX) {
    kfree(name);
    return (struct syscall_result){CALL_NOT_FOUND, 0};
  }
  struct acpi_tables_request *pending = (void *)bsp_request_prepare(BSP_SERVICE_ACPI_TABLES);
  pending->operation = ACPI_TABLES_QUERY;
  memcpy(pending->name, name, query.name_length);
  pending->name[query.name_length] = 0;
  kfree(name);
  bsp_request_submit_and_wait(&pending->request);
  status = pending->status;
  struct acpi_table_info info = pending->info;
  bsp_request_release(&pending->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_to_user(reply_address, &info, sizeof(info))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(info)};
}

static struct syscall_result directory_operation(struct directory_object *directory,
    uint64_t rights, uint64_t operation, const union directory_payload *input,
    uintptr_t reply_address, size_t reply_capacity)
{
  union directory_payload request = *input;
  if (operation == DIRECTORY_INFO) {
    return query_info(directory, rights, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_FILESYSTEM_INFO) {
    return filesystem_info(directory, rights, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_LOOKUP) {
    return lookup(directory, rights, &request.lookup, 0, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_LOOKUP_TYPED) {
    struct directory_child_request child = {
      .name = request.lookup_typed.name,
      .name_length = request.lookup_typed.name_length,
      .kind = DIRECTORY_KIND_ANY,
      .rights = request.lookup_typed.file_rights,
    };
    return lookup(directory, rights, &child, request.lookup_typed.directory_rights,
        reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_CREATE) {
    return create_child(directory, rights, &request.create, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_REMOVE) {
    return remove_child(directory, rights, &request.remove);
  }
  if (operation == DIRECTORY_SYNC) {
    if (directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) {
      struct npfs_request *pending = npfs_request_prepare(NPFS_SYNC);
      pending->job.node = directory->npfs;
      pending->job.fat_node = directory->fat32;
      if (directory->backing == DIRECTORY_FAT32) {
        pending->job.fat_actor = process_current()->control;
      }
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

struct syscall_result directory_call(struct directory_object *directory, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == DIRECTORY_PACKAGE_COMMANDS || operation == DIRECTORY_COMMAND_CAPTURE ||
      operation == DIRECTORY_COMMAND_INFO || operation == DIRECTORY_COMMAND_LOOKUP ||
      operation == DIRECTORY_COMMAND_BIND || operation == DIRECTORY_PACKAGE_ROOTS ||
      operation == DIRECTORY_PACKAGE_ROOTS_SELECT || operation == DIRECTORY_PACKAGE_SDK) {
    if (directory->repository_parent) return (struct syscall_result){CALL_DENIED, 0};
    return package_commands_call(directory, rights, operation, request_address, request_size,
        reply_address, reply_capacity);
  }
  if ((directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) &&
      (rights & ~DIRECTORY_RIGHTS)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (directory->backing == DIRECTORY_FAT32 && operation == DIRECTORY_FILESYSTEM_INFO) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (directory->repository_parent &&
      (operation == DIRECTORY_ENUMERATE || operation == DIRECTORY_LINK ||
       operation == DIRECTORY_FILESYSTEM_INFO)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  uint64_t required;
  switch (operation) {
  case DIRECTORY_INFO:
    required = 0;
    break;
  case DIRECTORY_LOOKUP:
  case DIRECTORY_LOOKUP_TYPED:
    required = DIRECTORY_RIGHT_LOOKUP;
    break;
  case DIRECTORY_ENUMERATE:
    required = DIRECTORY_RIGHT_ENUMERATE;
    break;
  case DIRECTORY_FILESYSTEM_INFO:
    if (directory->backing != DIRECTORY_NPFS && directory->backing != DIRECTORY_RAM) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    required = DIRECTORY_RIGHT_FILESYSTEM_INFO;
    break;
  case DIRECTORY_CREATE:
    required = DIRECTORY_RIGHT_CREATE;
    break;
  case DIRECTORY_LINK:
    required = DIRECTORY_LINK_SOURCE_RIGHTS;
    break;
  case DIRECTORY_REPOSITORY_VIEW:
    required = DIRECTORY_REPOSITORY_VIEW_RIGHTS;
    break;
  case DIRECTORY_SYNC:
    required = DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_REMOVE;
    break;
  case DIRECTORY_REMOVE:
  case DIRECTORY_RENAME:
    required = DIRECTORY_RIGHT_REMOVE;
    break;
  case ACPI_TABLES_INFO:
    if (directory->backing != DIRECTORY_ACPI) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    required = DIRECTORY_RIGHT_ENUMERATE;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (required && !(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if ((operation == DIRECTORY_LINK || operation == DIRECTORY_REPOSITORY_VIEW) &&
      (rights & required) != required) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  union directory_payload request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == DIRECTORY_REPOSITORY_VIEW) {
    return repository_view(directory, reply_address, reply_capacity);
  }
  if (operation == DIRECTORY_RENAME) {
    return rename_child(directory, rights, &request.rename);
  }
  if (operation == DIRECTORY_LINK) {
    return link_child(directory, rights, &request.link);
  }
  if (operation == ACPI_TABLES_INFO) {
    return acpi_table_query(&request, reply_address, reply_capacity);
  }
  size_t name_length = operation == DIRECTORY_LOOKUP_TYPED ? request.lookup_typed.name_length :
      operation == DIRECTORY_LOOKUP || operation == DIRECTORY_CREATE ?
      request.lookup.name_length : operation == DIRECTORY_REMOVE ? request.remove.name_length : 0;
  if (((directory->backing == DIRECTORY_NPFS || directory->backing == DIRECTORY_FAT32) &&
      name_length > NPFS_NAME_MAX) ||
      (directory->backing == DIRECTORY_HOST && name_length > VIRTIO_FS_NAME_MAX)) {
    return (struct syscall_result){CALL_NAME_TOO_LONG, 0};
  }
  char *source = NULL;
  enum call_status status = CALL_OK;
  if (operation == DIRECTORY_LOOKUP || operation == DIRECTORY_CREATE) {
    status = check_child_request(directory, rights, &request.lookup, reply_address, reply_capacity);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
  }
  if (operation == DIRECTORY_LOOKUP_TYPED) {
    status = check_typed_lookup(request.lookup_typed.file_rights,
        request.lookup_typed.directory_rights, reply_address, reply_capacity);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
  }
  /* Only private name fields are rewritten. Enumeration's destination remains
   * a user address; all named operations consume one immutable kernel capture. */
  if (operation == DIRECTORY_LOOKUP || operation == DIRECTORY_CREATE) {
    status = capture_name(request.lookup.name, request.lookup.name_length, &source);
    request.lookup.name = (uintptr_t)source;
  } else if (operation == DIRECTORY_LOOKUP_TYPED) {
    status = capture_name(request.lookup_typed.name, request.lookup_typed.name_length, &source);
    request.lookup_typed.name = (uintptr_t)source;
  } else if (operation == DIRECTORY_REMOVE) {
    status = capture_name(request.remove.name, request.remove.name_length, &source);
    request.remove.name = (uintptr_t)source;
  }

  struct syscall_result result = {status, 0};
  if (status == CALL_OK && directory->repository_parent && source) {
    if (!repository_name_allowed((uintptr_t)source, name_length)) {
      status = CALL_DENIED;
    } else if (operation == DIRECTORY_LOOKUP || operation == DIRECTORY_CREATE) {
      if (request.lookup.kind != DIRECTORY_KIND_FILE) {
        status = CALL_DENIED;
      }
    } else if (operation == DIRECTORY_REMOVE) {
      if (request.remove.kind == DIRECTORY_KIND_DIRECTORY) {
        status = CALL_DENIED;
      } else if (request.remove.kind == DIRECTORY_KIND_ANY) {
        request.remove.kind = DIRECTORY_KIND_FILE;
      }
    }
    result.status = status;
  }
  if (status == CALL_OK) {
    result = directory_operation(directory, rights, operation, &request,
        reply_address, reply_capacity);
  }
  if (source) {
    kfree(source);
  }
  return result;
}
