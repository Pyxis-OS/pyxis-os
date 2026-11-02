#include <abi/file.h>
#include <abi/console.h>
#include <abi/pipe.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/format.h>
#include <kernel/log.h>
#include <kernel/space.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/mm/heap.h>
#include <kernel/limit.h>
#include <kernel/object/environment.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/memory.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/user/launch.h>
#include <kernel/user/readiness_set.h>
#include <kernel/user_memory.h>

#include "launcher_internal.h"

struct launch_capture_block {
  struct launch_capture_block *next;
  _Alignas(uint64_t) unsigned char data[];
};

struct launcher {
  struct kernel_object object;
  struct execution_group *execution_group; /* Owned storage, no supervision. */
};

struct execution_group *launcher_execution_group(const struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_LAUNCHER);
  return ((const struct launcher *)object)->execution_group;
}

bool launcher_grant_allowed(struct execution_group *group, struct kernel_object *object)
{
  if (object_effective_type(object) != OBJECT_LAUNCHER) {
    return true;
  }
  if (object->type == OBJECT_LAUNCHER) {
    return execution_group_contains(group, launcher_execution_group(object));
  }
  struct forwarder_operation operation;
  struct capability_reference target;
  forwarder_operation_init(&operation);
  enum call_status status = forwarder_resolve(object, 0, 0, &operation, &target);
  bool allowed = status == CALL_OK && target.object->type == OBJECT_LAUNCHER &&
      execution_group_contains(group, launcher_execution_group(target.object));
  if (status == CALL_OK) {
    capability_release(&target);
  }
  forwarder_operation_release(&operation);
  return allowed;
}

static void destroy_launcher(struct kernel_object *object)
{
  struct execution_group *group = launcher_execution_group(object);
  if (group) {
    object_release(&group->object);
  }
  kfree(object);
}

static struct kernel_object *create_launcher(struct execution_group *group,
    struct call_limit *detail)
{
  KASSERT(arch_cpu_index() == 0);
  struct launcher *launcher = kmalloc(sizeof(*launcher));
  if (!launcher) {
    return NULL;
  }
  if (group && !object_retain(&group->object)) {
    call_limit_reference(detail);
    kfree(launcher);
    return NULL;
  }
  launcher->execution_group = group;
  object_init(&launcher->object, OBJECT_LAUNCHER, destroy_launcher);
  return &launcher->object;
}

struct kernel_object *launcher_create(void)
{
  return create_launcher(NULL, NULL);
}

static void create_execution_group(struct launcher_request *request)
{
  struct process *parent = request->parent;
  KASSERT(parent);
  request->result = CALL_NO_MEMORY;
  struct execution_group *group;
  request->result = execution_group_create(parent->space, request->execution_group, &group, &request->limit);
  if (request->result != CALL_OK) {
    return;
  }
  request->result = CALL_NO_MEMORY;
  struct kernel_object *launcher = create_launcher(group, &request->limit);
  if (!launcher && request->limit.reason) {
    request->result = CALL_LIMIT;
  }
  if (launcher) {
    struct capability_grant grants[2] = {0};
    enum capability_result result = capability_grant_retain(&group->object,
        EXECUTION_GROUP_RIGHT_CONTROL | EXECUTION_GROUP_RIGHT_WAIT, 0, &grants[0]);
    if (result == CAP_OK) {
      result = capability_grant_retain(launcher, LAUNCHER_RIGHT_LAUNCH, 0, &grants[1]);
    }
    if (result == CAP_OK) {
      result = forwarder_derive_grant_with_limit(&grants[0], task_forwarder_operation(), &request->limit);
    }
    if (result == CAP_OK) {
      result = forwarder_derive_grant_with_limit(&grants[1], task_forwarder_operation(), &request->limit);
    }
    if (result == CAP_OK) {
      result = capability_validate_grants(&parent->capabilities, grants, 2);
    }
    if (result == CAP_OK) {
      handle_t handles[2];
      request->result = task_operation_start() ?
          execution_group_install(group, &request->reservation,
              request->slots, grants, handles) : CALL_DENIED;
      if (request->result == CALL_OK) {
        request->execution_reply = (struct execution_group_create_reply){handles[0], handles[1]};
      }
    } else {
      request->result = result == CAP_LIMIT ?
          (request->limit.reason ? CALL_LIMIT : call_limit_reference(&request->limit)) :
          result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_DENIED;
    }
    for (size_t i = 0; i < 2; ++i) {
      capability_grant_release(&grants[i]);
    }
    object_release(launcher);
  }
  if (request->result != CALL_OK) {
    execution_group_abandon(group);
  }
  object_release(&group->object);
}

static void discard_capture(struct launch_capture *capture)
{
  for (size_t i = 0; i < capture->grant_count; ++i) {
    capability_grant_release(&capture->owned_grants[i]);
  }
  kfree(capture->owned_grants);
  kfree(capture->startup_storage.buffer);
  kfree(capture->space_entries);
  while (capture->blocks) {
    struct launch_capture_block *block = capture->blocks;
    capture->blocks = block->next;
    kfree(block);
  }
  image_capture_release(&capture->captured_image);
  if (capture->image) {
    if (capture->image_operation) {
      file_end_operation(capture->image);
    }
    object_release(&capture->image->object);
  }
  kfree(capture);
}

static enum call_status setup_space(struct launch_capture *capture)
{
  struct launch_space *request = &capture->space;
  if (space_name_taken(request->name)) {
    return CALL_BAD_REQUEST;
  }
  size_t words = space_cpu_words();
  uint64_t *ceiling = kmalloc(words * sizeof(*ceiling));
  if (!ceiling) {
    return CALL_NO_MEMORY;
  }
  memset(ceiling, 0, words * sizeof(*ceiling));
  for (size_t cpu = 0; cpu < request->cpu_count; ++cpu) {
    ceiling[cpu / 64] |= request->cpus[cpu / 64] & (UINT64_C(1) << (cpu % 64));
  }
  struct space *space = space_create(request->name, request->title, ceiling);
  space->terminal_control_enabled = request->terminal_control;
  space->screenshot_saver_enabled = request->screenshot_saver;
  space->jit_enabled = request->jit;
  space->clipboard_local_enabled = request->clipboard_local;
  space->clipboard_shared_enabled = request->clipboard_shared;
  capture->target_space = space;
  if (!capture->image) {
    space_report_unstarted(space, request->reason);
  }
  return CALL_OK;
}

static void finish_space(struct launch_capture *capture)
{
  if (capture->space_status != CALL_OK) {
    char reason[40];
    sprintf(reason, "init launch failed (status %u)", (unsigned)capture->space_status);
    space_report_unstarted(capture->target_space, reason);
  } else {
    if (capture->space.boot_focus) {
      space_boot_focus_arm(capture->target_space);
    }
    kinfo("userspace: space %s started\n", capture->target_space->name);
  }
}

void launcher_request_execute(struct launcher_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  struct execution_group *previous = object_cleanup_enter(request->execution_group ?
      request->execution_group : request->request.cleanup_group);
  struct vm_kernel_owner owner = {0};
  bool space_action = request->action == LAUNCH_CREATE_SPACE ||
      request->action == LAUNCH_SPACE_DEVICES || request->action == LAUNCH_SPACE_FINISH;
  if (space_action && !vm_kernel_owner_try(&owner)) {
    cpu_enable_interrupts();
    bool admitted;
    if (request->action == LAUNCH_SPACE_FINISH) {
      /* The child is already published or rollback completed. Finish its
       * fixed-owner reporting rather than inventing a post-publication failure. */
      vm_kernel_owner_cleanup(&owner);
      admitted = true;
    } else {
      admitted = vm_kernel_owner_acquire(&owner);
    }
    cpu_disable_interrupts();
    if (!admitted) {
      request->result = CALL_BUSY;
      goto done;
    }
  }
  switch (request->action) {
  case LAUNCH_CREATE_EXECUTION_GROUP:
    create_execution_group(request);
    break;
  case LAUNCH_CAPTURE_RAM:
    KASSERT(request->ram_file && request->ram_captured && !request->parent);
    request->result = file_ram_capture(request->ram_file, request->ram_captured,
        &request->limit);
    file_end_operation(request->ram_file);
    break;
  case LAUNCH_CREATE_SPACE:
    KASSERT(request->capture && !request->parent);
    request->result = setup_space(request->capture);
    break;
  case LAUNCH_SPACE_DEVICES:
    KASSERT(request->capture && request->capture->prepared_child);
    request->result = launcher_install_space_devices(request->capture->prepared_child,
        request->capture->target_space, &request->capture->startup,
        &request->capture->space_entries, &request->capture->limit);
    break;
  case LAUNCH_SPACE_FINISH:
    KASSERT(request->capture && request->capture->target_space);
    finish_space(request->capture);
    break;
  default:
    KASSERT(false);
  }
done:
  if (owner.root) {
    vm_kernel_owner_release(&owner);
  }
  capability_reservation_release(&request->reservation, request->slots);
  object_cleanup_leave(previous);
  request->capture = NULL;
  request->parent = NULL;
  request->execution_group = NULL;
}

static struct launcher_request *request_launch_service_reserved(enum launcher_action action,
    struct launch_capture *capture, struct execution_group *execution_group,
    struct capability_reservation *reservation, struct capability_reserved_slot *slots)
{
  struct launcher_request *request =
      (struct launcher_request *)bsp_request_prepare(BSP_SERVICE_LAUNCHER);
  *request = (struct launcher_request){
    .request = request->request,
    .action = action,
    .capture = capture,
    .execution_group = execution_group,
    .result = CALL_OK,
  };
  if (reservation) {
    KASSERT(reservation->count <= 2);
    request->reservation = *reservation;
    memcpy(request->slots, slots, reservation->count * sizeof(*slots));
    *reservation = (struct capability_reservation){0};
  }
  if (action == LAUNCH_CREATE_EXECUTION_GROUP) {
    request->parent = process_current();
    KASSERT(request->parent);
  }
  bsp_request_submit_and_wait(&request->request);
  return request;
}

static struct launcher_request *request_launch_service(enum launcher_action action,
    struct launch_capture *capture, struct execution_group *execution_group)
{
  return request_launch_service_reserved(action, capture, execution_group, NULL, NULL);
}

static struct launch_capture *allocate_launch_capture(size_t grant_count)
{
  struct launch_capture *capture = kmalloc(sizeof(*capture));
  if (!capture) {
    return NULL;
  }
  memset(capture, 0, sizeof(*capture));
  if (grant_count) {
    size_t grant_bytes = grant_count * sizeof(*capture->owned_grants);
    size_t slot_bytes = grant_count * sizeof(*capture->child_slots);
    size_t handle_bytes = grant_count * sizeof(*capture->child_handles);
    capture->owned_grants = kmalloc(grant_bytes + slot_bytes + handle_bytes);
    if (!capture->owned_grants) {
      kfree(capture);
      return NULL;
    }
    memset(capture->owned_grants, 0, grant_bytes);
    capture->child_slots = (void *)((unsigned char *)capture->owned_grants + grant_bytes);
    capture->child_handles = (void *)((unsigned char *)capture->child_slots + slot_bytes);
  }
  capture->grant_count = grant_count;
  return capture;
}

static void discard_launch_capture(struct launch_capture *capture)
{
  bool entered = !task_vm_kernel_active();
  if (entered) {
    task_vm_kernel_enter();
  }
  discard_capture(capture);
  if (entered) {
    task_vm_kernel_leave();
  }
}

static enum call_status launch_process(struct launch_capture *capture,
    struct execution_group *execution_group, struct capability_reservation *reservation,
    struct capability_reserved_slot *slots, handle_t *child,
    struct readiness_set_registration *registration, struct call_limit *detail)
{
  task_vm_kernel_enter();
  struct execution_group *previous = object_cleanup_enter(execution_group ?
      execution_group : task_cleanup_group());
  enum call_status status;
  if (task_operation_start()) {
    status = launcher_start(capture, process_current(), arch_cpu_index(), execution_group,
        reservation, slots, child, registration, detail);
  } else {
    *child = HANDLE_INVALID;
    status = CALL_DENIED;
  }
  capability_reservation_release(reservation, slots);
  discard_capture(capture);
  object_cleanup_leave(previous);
  task_vm_kernel_leave();
  return status;
}

static enum call_status prepare_launch_batch(struct launch_preparation *group,
    struct launch_capture *capture, struct execution_group *execution_group,
    struct call_limit *detail)
{
  task_vm_kernel_enter();
  struct execution_group *previous = object_cleanup_enter(execution_group ?
      execution_group : task_cleanup_group());
  enum call_status status = launcher_batch_prepare(group, capture, process_current(),
      arch_cpu_index(), execution_group, detail);
  discard_capture(capture);
  object_cleanup_leave(previous);
  task_vm_kernel_leave();
  return status;
}

static enum call_status publish_launch_batch(struct launch_preparation *group, handle_t *children,
    struct call_limit *detail)
{
  task_vm_kernel_enter();
  enum call_status status = task_operation_start() ?
      launcher_batch_publish(group, children, NULL, NULL, detail) : CALL_DENIED;
  launcher_batch_discard(group);
  task_vm_kernel_leave();
  return status;
}

static void discard_launch_batch(struct launch_preparation *group)
{
  task_vm_kernel_enter();
  launcher_batch_discard(group);
  task_vm_kernel_leave();
}

static void *capture_allocate(struct launch_capture *capture, size_t bytes)
{
  if (capture->error != CALL_OK || !bytes) {
    return NULL;
  }
  if (bytes > SIZE_MAX - sizeof(struct launch_capture_block)) {
    capture->error = call_limit_encoding(&capture->limit);
    return NULL;
  }
  struct launch_capture_block *block = kmalloc(sizeof(*block) + bytes);
  if (!block) {
    capture->error = CALL_NO_MEMORY;
    return NULL;
  }
  block->next = capture->blocks;
  capture->blocks = block;
  return block->data;
}

/* Each input byte is copied once, without retaining a mapping lease across an
 * allocation or a bounded-work yield. Published capture pointers never move. */
void *launcher_capture_array(struct launch_capture *capture, uintptr_t address,
    size_t count, size_t item_size)
{
  if (!count || capture->error != CALL_OK) {
    return NULL;
  }
  if (!item_size || count > SIZE_MAX / item_size) {
    capture->error = call_limit_encoding(&capture->limit);
    return NULL;
  }
  size_t bytes = count * item_size;
  if (bytes > UINTPTR_MAX - address) {
    capture->error = CALL_BAD_BUFFER;
    return NULL;
  }
  uint8_t *array = capture_allocate(capture, bytes);
  if (!array) {
    return NULL;
  }
  for (size_t offset = 0; offset < bytes;) {
    size_t step = bytes - offset;
    if (step > PAGE_SIZE) {
      step = PAGE_SIZE;
    }
    if (!copy_from_user(array + offset, address + offset, step)) {
      capture->error = CALL_BAD_BUFFER;
      return NULL;
    }
    offset += step;
    if (!(offset % (64 * PAGE_SIZE))) {
      task_yield_masked();
    }
  }
  return array;
}

static bool measure_user_string(struct launch_capture *capture, uintptr_t source,
    size_t *length)
{
  *length = 0;
  for (size_t pages = 0;; ++pages) {
    if (*length > UINTPTR_MAX - source) {
      capture->error = CALL_BAD_BUFFER;
      return false;
    }
    uintptr_t address = source + *length;
    size_t bytes = PAGE_SIZE - (address & (PAGE_SIZE - 1));
    struct user_buffer_lease lease;
    if (!user_buffer_lease_acquire(&lease)) {
      capture->error = CALL_BAD_BUFFER;
      return false;
    }
    bool valid = user_buffer_check_leased(&lease, address, bytes, USER_BUFFER_READ);
    size_t step = 0;
    if (valid) {
      while (step < bytes && ((const char *)address)[step]) {
        ++step;
      }
    }
    user_buffer_lease_release(&lease);
    if (!valid) {
      capture->error = CALL_BAD_BUFFER;
      return false;
    }
    if (*length > SIZE_MAX - step - (step < bytes ? 1 : 0)) {
      capture->error = call_limit_encoding(&capture->limit);
      return false;
    }
    *length += step;
    if (step < bytes) {
      ++*length;
      return true;
    }
    if ((pages + 1) % 64 == 0) {
      task_yield_masked();
    }
  }
}

static const char *capture_string(struct launch_capture *capture, uintptr_t source)
{
  if (capture->error != CALL_OK) {
    return NULL;
  }
  size_t length;
  if (!measure_user_string(capture, source, &length)) {
    return NULL;
  }
  char *text = capture_allocate(capture, length);
  if (!text) {
    return NULL;
  }
  for (size_t offset = 0, pages = 0; offset < length; ++pages) {
    size_t step = PAGE_SIZE - ((source + offset) & (PAGE_SIZE - 1));
    if (step > length - offset) {
      step = length - offset;
    }
    size_t copied;
    enum user_string_result result = copy_string_from_user(text + offset,
        source + offset, step, &copied);
    if (result == USER_STRING_BAD_BUFFER) {
      capture->error = CALL_BAD_BUFFER;
      return NULL;
    }
    if (result == USER_STRING_OK) {
      if (offset + copied != length) {
        capture->error = CALL_BAD_REQUEST;
        return NULL;
      }
      return text;
    }
    offset += step;
    if ((pages + 1) % 64 == 0) {
      task_yield_masked();
    }
  }
  /* The input grew after measurement. No partial string becomes usable. */
  capture->error = CALL_BAD_REQUEST;
  return NULL;
}

static struct process_environment_entry *capture_entries(struct launch_capture *capture,
    uintptr_t address, size_t count)
{
  _Static_assert(sizeof(struct launch_environment_entry) == sizeof(struct process_environment_entry),
                 "environment capture storage");
  struct process_environment_entry *entries = launcher_capture_array(capture, address,
      count, sizeof(*entries));
  if (!entries) {
    return NULL;
  }
  for (size_t i = 0; i < count && capture->error == CALL_OK; ++i) {
    struct launch_environment_entry wire;
    memcpy(&wire, &entries[i], sizeof(wire));
    if ((wire.flags & ~ENVIRONMENT_EXPORTED) ||
        (wire.kind != ENVIRONMENT_CAP && wire.kind != ENVIRONMENT_TEXT) ||
        (wire.kind == ENVIRONMENT_CAP && (wire.value >= capture->grant_count || wire.length))) {
      capture->error = CALL_BAD_REQUEST;
      break;
    }
    entries[i].name = capture_string(capture, wire.name);
    entries[i].kind = wire.kind;
    entries[i].flags = wire.flags;
    entries[i].length = wire.length;
    entries[i].value = wire.kind == ENVIRONMENT_TEXT ?
        (uintptr_t)capture_string(capture, wire.value) : wire.value;
    if (wire.kind == ENVIRONMENT_TEXT && capture->error == CALL_OK &&
        strlen((const char *)(uintptr_t)entries[i].value) != wire.length) {
      capture->error = CALL_BAD_REQUEST;
    }
  }
  return entries;
}

/* A launch may give the child FILE_RIGHT_PROGRAM on a file the caller can
 * read: the child may then map it as its dynamic executable, which the caller
 * could already launch. Any other grant only attenuates. */
static enum capability_result acquire_launch_grant(struct launch_capture *capture,
    const struct launch_grant *grant, struct capability_grant *owned)
{
  struct capability_table *table = &process_current()->capabilities;
  enum capability_result status = capability_grant_acquire_with_limit(table, grant->source,
      grant->rights, grant->transport, owned, &capture->limit);
  if (status != CAP_DENIED || !(grant->rights & FILE_RIGHT_PROGRAM)) {
    return status;
  }
  status = capability_grant_acquire_with_limit(table, grant->source,
      grant->rights & ~FILE_RIGHT_PROGRAM, grant->transport, owned, &capture->limit);
  if (status != CAP_OK) {
    return status;
  }
  if (object_effective_type(owned->object) != OBJECT_FILE ||
      !(owned->rights & FILE_RIGHT_READ)) {
    capability_grant_release(owned);
    return CAP_DENIED;
  }
  owned->rights |= FILE_RIGHT_PROGRAM;
  return CAP_OK;
}

static void capture_startup(struct launch_capture *capture, const struct launch_request *source)
{
  capture->grants = launcher_capture_array(capture, source->grants, source->grant_count,
      sizeof(*capture->grants));
  for (size_t i = 0; i < capture->grant_count && capture->error == CALL_OK; ++i) {
    enum capability_result status = acquire_launch_grant(capture, &capture->grants[i],
        &capture->owned_grants[i]);
    if (status != CAP_OK) {
      capture->error = status == CAP_LIMIT ? CALL_LIMIT :
          status == CAP_BAD_HANDLE ? CALL_BAD_HANDLE :
          status == CAP_DENIED ? CALL_DENIED : CALL_BAD_REQUEST;
    }
  }
  struct process_startup *startup = &capture->startup;
  startup->entry_count = source->entry_count;
  startup->entries = capture_entries(capture, source->entries, source->entry_count);
  startup->working_directory_count = source->working_directory_count;
  startup->working_directories = launcher_capture_array(capture, source->working_directories,
      source->working_directory_count, sizeof(handle_t));
  for (size_t i = 0; i < source->working_directory_count && capture->error == CALL_OK; ++i) {
    size_t index = startup->working_directories[i];
    if (index >= source->grant_count ||
        object_effective_type(capture->owned_grants[index].object) != OBJECT_DIRECTORY) {
      capture->error = CALL_BAD_REQUEST;
    }
  }
  if (source->working_path) {
    startup->working_path = capture_string(capture, source->working_path);
  }

  startup->argc = source->argc;
  const char **argv = launcher_capture_array(capture, source->argv, source->argc, sizeof(*argv));
  startup->argv = argv;
  for (size_t i = 0; i < source->argc && capture->error == CALL_OK; ++i) {
    argv[i] = capture_string(capture, (uintptr_t)argv[i]);
  }
}

enum call_status launcher_capture_metadata(const struct launch_request *request,
    struct launch_capture **result, struct call_limit *detail)
{
  *result = NULL;
  enum call_status policy = environment_seed_admit(request->entry_count, 0, detail);
  if (policy != CALL_OK) {
    return policy;
  }
  size_t initial_stack_bytes;
  enum call_status admitted = user_initial_stack_size(request->initial_stack_bytes,
      &initial_stack_bytes, detail);
  if (admitted != CALL_OK) {
    return admitted;
  }
  if (capability_count_admit(request->grant_count, detail) != CAP_OK) {
    return CALL_LIMIT;
  }
  size_t per_grant = sizeof(struct capability_grant) +
      sizeof(struct capability_reserved_slot) + sizeof(handle_t);
  if (request->grant_count > SIZE_MAX / per_grant) {
    return call_limit_encoding(detail);
  }
  struct capability_reference image;
  enum capability_result lookup = capability_acquire_with_limit(&process_current()->capabilities,
      request->image, FILE_RIGHT_READ, 0, &image, detail);
  if (lookup != CAP_OK) {
    return lookup == CAP_LIMIT ? CALL_LIMIT :
        lookup == CAP_BAD_HANDLE ? CALL_BAD_HANDLE : CALL_DENIED;
  }
  lookup = capability_reference_resolve_with_limit(&image, detail);
  if (lookup != CAP_OK) {
    capability_release(&image);
    return lookup == CAP_LIMIT ? CALL_LIMIT : lookup == CAP_NOT_FOUND ? CALL_NOT_FOUND : CALL_DENIED;
  }
  if (image.object->type != OBJECT_FILE) {
    capability_release(&image);
    return CALL_WRONG_TYPE;
  }

  struct launch_capture *capture = allocate_launch_capture(request->grant_count);
  if (!capture) {
    capability_release(&image);
    return CALL_NO_MEMORY;
  }
  capture->image_rights = image.rights;
  capture->image = (struct file_object *)image.object;
  image.object = NULL;
  capability_release(&image);
  capture->initial_stack_bytes = initial_stack_bytes;
  if (task_stop_requested()) {
    discard_launch_capture(capture);
    return CALL_ENDPOINT_CLOSED;
  }
  capture_startup(capture, request);
  if (capture->error == CALL_OK) {
    capture->error = process_startup_measure(&capture->startup,
        &capture->startup_storage, &capture->limit);
  }
  if (capture->error == CALL_OK) {
    size_t bytes = capture->startup_storage.metadata + capture->startup_storage.arguments;
    capture->startup_storage.buffer = kmalloc(bytes);
    if (!capture->startup_storage.buffer) {
      capture->error = CALL_NO_MEMORY;
    }
  }
  if (capture->error != CALL_OK) {
    enum call_status error = capture->error;
    if (detail) {
      *detail = capture->limit;
    }
    discard_launch_capture(capture);
    return error;
  }
  *result = capture;
  return CALL_OK;
}

enum call_status launcher_capture_file(struct file_object *file, uint64_t rights,
    struct image_capture *captured, bool *operation, struct call_limit *limit)
{
  KASSERT(file && !*operation && !captured->address);
  if (file->backing == FILE_NPFS || file->backing == FILE_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_CAPTURE);
    pending->job.node = file->npfs;
    pending->job.fat_node = file->fat32;
    /* IMPORT and PROGRAM authorize mapping, not reading: the provider sees
     * only the content rights its node was opened with. */
    pending->job.rights = rights & FILE_RIGHTS;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    *limit = pending->job.limit;
    *captured = pending->job.captured;
    pending->job.captured = (struct image_capture){0};
    npfs_request_release(pending);
    if (status != CALL_OK) {
      return status;
    }
  } else if (file->backing == FILE_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_CAPTURE);
    pending->node = file->host;
    hostfs_request_submit_and_wait(pending);
    if (pending->status != CALL_OK) {
      enum call_status error = pending->status;
      *limit = pending->limit;
      hostfs_request_release(pending);
      return error;
    }
    *captured = pending->captured;
    pending->captured = (struct image_capture){0};
    hostfs_request_release(pending);
  } else if (!file_begin_operation(file)) {
    return CALL_ENDPOINT_CLOSED;
  } else if (file->backing == FILE_RAM) {
    /* Lend the stable file operation. Copy and mapping access stay on the BSP,
     * which ends the operation. */
    struct launcher_request *pending =
        (struct launcher_request *)bsp_request_prepare(BSP_SERVICE_LAUNCHER);
    *pending = (struct launcher_request){
      .request = pending->request,
      .action = LAUNCH_CAPTURE_RAM,
      .result = CALL_OK,
      .ram_file = file,
      .ram_captured = captured,
    };
    bsp_request_submit_and_wait(&pending->request);
    enum call_status status = pending->result;
    *limit = pending->limit;
    bsp_request_release(&pending->request);
    if (status != CALL_OK) {
      return status;
    }
  } else {
    *operation = true;
  }
  return task_stop_requested() ? CALL_ENDPOINT_CLOSED : CALL_OK;
}

enum call_status launcher_capture_image(struct launch_capture *capture)
{
  KASSERT(capture->image);
  return launcher_capture_file(capture->image, capture->image_rights,
      &capture->captured_image, &capture->image_operation, &capture->limit);
}

enum call_status launcher_capture_request(const struct launch_request *request,
    struct launch_capture **result, struct call_limit *detail)
{
  enum call_status status = launcher_capture_metadata(request, result, detail);
  if (status == CALL_OK) {
    status = launcher_capture_image(*result);
    if (status != CALL_OK) {
      if (detail) {
        *detail = (*result)->limit;
      }
      discard_launch_capture(*result);
      *result = NULL;
    }
  }
  return status;
}

struct launch_capture *launcher_capture_empty(void)
{
  return allocate_launch_capture(0);
}

void launcher_capture_discard(struct launch_capture *capture)
{
  discard_launch_capture(capture);
}

enum call_status launcher_submit_space(struct launch_capture *capture, handle_t *child,
    struct call_limit *detail)
{
  *child = HANDLE_INVALID;
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (capture->image) {
    enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
    if (reserved != CAP_OK) {
      launcher_capture_discard(capture);
      return reserved == CAP_LIMIT ? call_limit_set(detail, CALL_LIMIT_HANDLE_ENCODING,
          0, UINT64_C(1) << 32, CALL_LIMIT_HAS_LIMIT) : CALL_NO_MEMORY;
    }
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    launcher_capture_discard(capture);
    return CALL_ENDPOINT_CLOSED;
  }
  task_vm_kernel_enter();
  struct launcher_request *request = request_launch_service(LAUNCH_CREATE_SPACE,
      capture, NULL);
  enum call_status status = request->result;
  bsp_request_release(&request->request);
  if (status == CALL_OK && capture->image) {
    status = launcher_prepare_space(capture, process_current(), &reservation, &slot, child, detail);
    capture->space_status = status;
    request = request_launch_service(LAUNCH_SPACE_FINISH, capture, NULL);
    bsp_request_release(&request->request);
  }
  capability_reservation_release(&reservation, &slot);
  if (status == CALL_LIMIT && detail && !detail->reason) {
    *detail = capture->limit;
  }
  discard_capture(capture);
  task_vm_kernel_leave();
  return status;
}

enum call_status launcher_request_space_devices(struct launch_capture *capture,
    struct process *child, struct process_startup *startup,
    struct process_environment_entry **entries)
{
  KASSERT(task_vm_kernel_active() && !capture->prepared_child && capture->space_entries);
  process_prepare_require(child);
  capture->prepared_child = child;
  capture->startup = *startup;
  struct launcher_request *request = request_launch_service(LAUNCH_SPACE_DEVICES,
      capture, NULL);
  enum call_status status = request->result;
  bsp_request_release(&request->request);
  capture->prepared_child = NULL;
  *startup = capture->startup;
  *entries = capture->space_entries;
  capture->space_entries = NULL;
  return status;
}

static struct syscall_result launch_one(struct execution_group *execution_group,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, struct call_limit *detail)
{
  bool observed = operation == LAUNCHER_LAUNCH_OBSERVED;
  struct launch_observed_request input = {0};
  size_t input_size = observed ? sizeof(input) : sizeof(input.request);
  size_t output_size = observed ? sizeof(struct launch_observed_reply) : sizeof(handle_t);
  if (request_size != input_size || reply_capacity < output_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&input, request_address, input_size) ||
      !user_buffer_check(reply_address, output_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
  if (reserved != CAP_OK) {
    return (struct syscall_result){reserved == CAP_LIMIT ? call_limit_set(detail,
        CALL_LIMIT_HANDLE_ENCODING, 0, UINT64_C(1) << 32, CALL_LIMIT_HAS_LIMIT) : CALL_NO_MEMORY, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct launch_capture *capture;
  enum call_status status = launcher_capture_request(&input.request, &capture, detail);
  if (status != CALL_OK) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){status, 0};
  }
  struct readiness_process_watch *watch = NULL;
  if (observed) {
    struct capability_reference reference = {0};
    reserved = capability_acquire_with_limit(&process_current()->capabilities, input.set,
        READINESS_SET_RIGHT_CONTROL, 0, &reference, detail);
    if (reserved == CAP_OK) {
      status = readiness_process_watch_prepare(process_current(), &reference, input.tag,
          &watch, detail); /* Consumes the reference on every outcome. */
    } else {
      status = reserved == CAP_DENIED ? CALL_DENIED : reserved == CAP_LIMIT ? CALL_LIMIT :
          reserved == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_HANDLE;
    }
    if (status != CALL_OK) {
      capability_reservation_release(&reservation, &slot);
      discard_launch_capture(capture);
      return (struct syscall_result){status, 0};
    }
    capture->watch = watch;
  }

  /* Capture borrows WATCH. Keep its loan until the final reply is copied, so
   * unknown admission never leaves an unclaimed registration behind. */
  struct launch_observed_reply reply = {0};
  status = launch_process(capture, execution_group, &reservation, &slot, &reply.child,
      &reply.registration, detail);
  bool published = status == CALL_OK;
  if (published && !copy_returned_handles_to_user(reply_address, &reply, output_size,
      &reply.child, 1)) {
    status = CALL_BAD_BUFFER;
    published = false; /* Observation only; the admitted child is not replayed or killed. */
  }
  if (watch) {
    readiness_process_watch_finish(watch, published);
  }
  return (struct syscall_result){status, status == CALL_OK ? output_size : 0};
}

static struct syscall_result batch_failure(uintptr_t address, size_t count,
    uint64_t failed_index, enum call_status status)
{
  struct launch_batch_reply header = {.failed_index = failed_index};
  if (!copy_to_user(address, &header, sizeof(header))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  handle_t empty[64] = {0};
  for (size_t offset = 0; offset < count;) {
    size_t step = count - offset;
    if (step > sizeof(empty) / sizeof(empty[0])) {
      step = sizeof(empty) / sizeof(empty[0]);
    }
    if (!copy_to_user(address + sizeof(header) + offset * sizeof(handle_t),
        empty, step * sizeof(handle_t))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    offset += step;
    if (!(offset % (64 * 64))) {
      task_yield_masked();
    }
  }
  return (struct syscall_result){status, sizeof(header) + count * sizeof(handle_t)};
}

static struct syscall_result launch_batch(struct execution_group *execution_group,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, struct call_limit *detail)
{
  size_t header_bytes = sizeof(struct launch_batch_reply);
  if (reply_capacity < header_bytes) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }
  if (!user_buffer_check(reply_address, header_bytes, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request_size != sizeof(struct launch_batch_request)) {
    return batch_failure(reply_address, 0, LAUNCH_NO_STAGE, CALL_BAD_REQUEST);
  }
  struct launch_batch_request batch;
  if (!copy_from_user(&batch, request_address, sizeof(batch))) {
    return batch_failure(reply_address, 0, LAUNCH_NO_STAGE, CALL_BAD_BUFFER);
  }
  if (!batch.count) {
    return batch_failure(reply_address, 0, LAUNCH_NO_STAGE, CALL_BAD_REQUEST);
  }
  if (batch.count > (SIZE_MAX - header_bytes) / sizeof(handle_t) ||
      batch.count > SIZE_MAX / sizeof(struct launch_request) ||
      batch.count > SIZE_MAX / sizeof(struct launch_capture *)) {
    return batch_failure(reply_address, 0, LAUNCH_NO_STAGE, call_limit_encoding(detail));
  }
  if (capability_count_admit(batch.count, detail) != CAP_OK) {
    return batch_failure(reply_address, 0, LAUNCH_NO_STAGE, CALL_LIMIT);
  }
  size_t reply_bytes = header_bytes + batch.count * sizeof(handle_t);
  if (reply_capacity < reply_bytes) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }
  if (!user_buffer_check(reply_address, reply_bytes, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct launch_batch_reply *reply = kmalloc(reply_bytes);
  struct launch_capture **captures = kmalloc(batch.count * sizeof(*captures));
  struct launch_capture *request_owner = allocate_launch_capture(0);
  enum call_status status = CALL_NO_MEMORY;
  uint64_t failed_index = LAUNCH_NO_STAGE;
  struct launch_preparation *group = NULL;
  size_t captured = 0;
  if (!reply || !captures || !request_owner) {
    goto done;
  }
  memset(reply, 0, reply_bytes);
  reply->failed_index = LAUNCH_NO_STAGE;
  struct launch_request *requests = launcher_capture_array(request_owner, batch.requests,
      batch.count, sizeof(*requests));
  if (!requests) {
    status = request_owner->error;
    if (detail) {
      *detail = request_owner->limit;
    }
    goto done;
  }
  group = launcher_batch_create(batch.count, &status, detail);
  if (!group) {
    goto done;
  }
  /* Every stage's owned metadata and serialization scratch exists before the
   * first source payload is read. Never hold multiple FILE operations. */
  for (; captured < batch.count; ++captured) {
    status = launcher_capture_metadata(&requests[captured], &captures[captured], detail);
    if (status != CALL_OK) {
      failed_index = captured;
      goto done;
    }
  }
  status = launcher_batch_reserve(group, batch.count, detail);
  if (status != CALL_OK) {
    goto done;
  }
  for (size_t i = 0; i < batch.count; ++i) {
    if (task_stop_requested()) {
      status = CALL_ENDPOINT_CLOSED;
    } else {
      status = launcher_capture_image(captures[i]);
    }
    if (status == CALL_OK) {
      struct launch_capture *capture = captures[i];
      captures[i] = NULL;
      status = prepare_launch_batch(group, capture, execution_group, detail);
    } else if (detail) {
      *detail = captures[i]->limit;
    }
    if (status != CALL_OK) {
      failed_index = i;
      goto done;
    }
  }
  status = publish_launch_batch(group, reply->children, detail);
  group = NULL;
  if (status == CALL_OK) {
    reply->child_count = batch.count;
    struct syscall_result result = {CALL_OK, reply_bytes};
    for (size_t offset = 0; offset < batch.count;) {
      size_t step = batch.count - offset;
      if (step > 64) {
        step = 64;
      }
      if (!copy_to_user(reply_address + header_bytes + offset * sizeof(handle_t),
          reply->children + offset, step * sizeof(handle_t))) {
        result = (struct syscall_result){CALL_BAD_BUFFER, 0};
        break;
      }
      offset += step;
      if (offset < batch.count && !(offset % (64 * 64))) {
        task_yield_masked();
      }
    }
    if (result.status == CALL_OK && !copy_to_user(reply_address, reply, header_bytes)) {
      result = (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (result.status != CALL_OK) {
      capability_close_returned(&process_current()->capabilities, reply->children, batch.count);
    }
    discard_launch_capture(request_owner);
    kfree(captures);
    kfree(reply);
    return result;
  }

done:
  if (group) {
    discard_launch_batch(group);
  }
  for (size_t i = 0; i < captured; ++i) {
    if (captures[i]) {
      launcher_capture_discard(captures[i]);
    }
  }
  if (request_owner) {
    discard_launch_capture(request_owner);
  }
  kfree(captures);
  kfree(reply);
  return batch_failure(reply_address, batch.count, failed_index, status);
}

static struct syscall_result create_group(struct execution_group *parent_group, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, struct call_limit *detail)
{
  if (request_size || reply_capacity < sizeof(struct execution_group_create_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct execution_group_create_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[2];
  enum capability_result reserved = capability_request_reservation(2, &reservation, slots);
  if (reserved != CAP_OK) {
    return (struct syscall_result){reserved == CAP_LIMIT ? call_limit_set(detail, CALL_LIMIT_HANDLE_ENCODING,
        0, UINT64_C(1) << 32, CALL_LIMIT_HAS_LIMIT) : CALL_NO_MEMORY, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct launcher_request *request = request_launch_service_reserved(
      LAUNCH_CREATE_EXECUTION_GROUP, NULL, parent_group, &reservation, slots);
  enum call_status status = request->result;
  if (status == CALL_LIMIT && detail) {
    *detail = request->limit;
  }
  struct execution_group_create_reply reply = request->execution_reply;
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  handle_t handles[] = {reply.supervision, reply.launcher};
  if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), handles, 2)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result launcher_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, struct call_limit *detail)
{
  struct process *parent = process_current();
  struct execution_group *group = launcher_execution_group(object);
  if (operation != LAUNCHER_CREATE_GROUP && operation != LAUNCHER_LAUNCH &&
      operation != LAUNCHER_LAUNCH_BATCH && operation != LAUNCHER_LAUNCH_OBSERVED) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & LAUNCHER_RIGHT_LAUNCH) ||
      !execution_group_contains(parent->execution_group, group)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  /* The launcher selects the fixed scope. Constructor and launch preparation
   * own an admission token across waits; final publication rechecks the tree. */
  if (group && group->space != parent->space) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  enum call_status status = execution_group_launch_begin(group, parent->space, detail);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* Capturing can allocate and retire a selected image even before child
   * preparation starts. Its cleanup must join this launcher's scope too. */
  struct execution_group *previous = object_cleanup_enter(group ? group : task_cleanup_group());
  if (operation == LAUNCHER_CREATE_GROUP) {
    struct syscall_result result = create_group(group, request_size, reply_address, reply_capacity, detail);
    object_cleanup_leave(previous);
    execution_group_launch_end(group);
    return result;
  }
  struct syscall_result result = operation == LAUNCHER_LAUNCH_BATCH ?
      launch_batch(group, request_address, request_size, reply_address, reply_capacity, detail) :
      launch_one(group, operation, request_address, request_size, reply_address, reply_capacity, detail);
  object_cleanup_leave(previous);
  execution_group_launch_end(group);
  return result;
}
