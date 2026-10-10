#include <abi/file.h>
#include <abi/console.h>
#include <abi/namespace.h>
#include <abi/pipe.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user/launch.h>
#include <kernel/user_memory.h>

#include "launcher_internal.h"

struct launcher {
  struct kernel_object object;
  struct execution_group *execution_group; /* Owned storage, no supervision. */
};

struct execution_group *launcher_execution_group(const struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_LAUNCHER);
  return ((const struct launcher *)object)->execution_group;
}

static void destroy_launcher(struct kernel_object *object)
{
  struct execution_group *group = launcher_execution_group(object);
  if (group) {
    object_release(&group->object);
  }
  kfree(object);
}

static struct kernel_object *create_launcher(struct execution_group *group)
{
  KASSERT(arch_cpu_index() == 0);
  struct launcher *launcher = kmalloc(sizeof(*launcher));
  if (!launcher) {
    return NULL;
  }
  if (group && !object_retain(&group->object)) {
    kfree(launcher);
    return NULL;
  }
  launcher->execution_group = group;
  object_init(&launcher->object, OBJECT_LAUNCHER, destroy_launcher);
  return &launcher->object;
}

struct kernel_object *launcher_create(void)
{
  return create_launcher(NULL);
}

static void create_execution_group(struct launcher_request *request)
{
  struct process *parent = request->parent;
  KASSERT(parent && !parent->execution_group);
  request->result = CALL_NO_MEMORY;
  struct execution_group *group = execution_group_create(parent->space);
  if (!group) {
    return;
  }
  struct kernel_object *launcher = create_launcher(group);
  if (launcher) {
    struct capability_grant grants[2] = {0};
    enum capability_result result = capability_grant_retain(&group->object,
        EXECUTION_GROUP_RIGHT_CONTROL | EXECUTION_GROUP_RIGHT_WAIT, 0, &grants[0]);
    if (result == CAP_OK) {
      result = capability_grant_retain(launcher, LAUNCHER_RIGHT_LAUNCH, 0, &grants[1]);
    }
    if (result == CAP_OK) {
      result = capability_validate_grants(&parent->capabilities, grants, 2);
    }
    if (result == CAP_OK) {
      handle_t handles[2];
      capability_install_reserved(&request->reservation, request->slots, grants, 2, handles);
      request->execution_reply = (struct execution_group_create_reply){handles[0], handles[1]};
      request->result = CALL_OK;
    } else {
      request->result = result == CAP_LIMIT ? CALL_LIMIT : CALL_DENIED;
    }
    for (size_t i = 0; i < 2; ++i) {
      capability_grant_release(&grants[i]);
    }
    object_release(launcher);
  }
  object_release(&group->object);
}

static void discard_capture(struct launch_capture *capture)
{
  for (size_t i = 0; i < capture->grant_count; ++i) {
    capability_grant_release(&capture->owned_grants[i]);
  }
  kfree(capture->owned_grants);
  image_capture_release(&capture->captured_image);
  if (capture->image) {
    object_release(&capture->image->object);
  }
  kfree(capture);
}

void launcher_request_execute(struct launcher_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  struct execution_group *previous = object_cleanup_enter(request->execution_group ?
      request->execution_group : request->request.cleanup_group);
  switch (request->action) {
  case LAUNCH_CREATE_EXECUTION_GROUP:
    create_execution_group(request);
    break;
  case LAUNCH_ALLOCATE:
    request->capture_result = kmalloc(sizeof(*request->capture_result));
    if (request->capture_result) {
      struct launch_capture *capture = request->capture_result;
      memset(capture, 0, sizeof(*capture));
      if (request->grant_count) {
        size_t grant_bytes = request->grant_count * sizeof(*capture->owned_grants);
        size_t slot_bytes = request->grant_count * sizeof(*capture->child_slots);
        size_t handle_bytes = request->grant_count * sizeof(*capture->child_handles);
        capture->owned_grants = kmalloc(grant_bytes + slot_bytes + handle_bytes);
        if (!capture->owned_grants) {
          kfree(capture);
          request->capture_result = NULL;
          request->result = CALL_NO_MEMORY;
          break;
        }
        memset(capture->owned_grants, 0, grant_bytes);
        capture->child_slots = (void *)((unsigned char *)capture->owned_grants + grant_bytes);
        capture->child_handles = (void *)((unsigned char *)capture->child_slots + slot_bytes);
      }
      capture->grant_count = request->grant_count;
    } else {
      request->result = CALL_NO_MEMORY;
    }
    break;
  case LAUNCH_BATCH_CREATE:
    request->group_result = launcher_batch_create();
    if (!request->group_result) {
      request->result = CALL_NO_MEMORY;
    }
    break;
  case LAUNCH_CAPTURE_RAM:
    KASSERT(request->capture && !request->parent && !request->group);
    request->result = file_ram_capture(request->capture->image,
        &request->capture->captured_image);
    file_end_operation(request->capture->image);
    break;
  case LAUNCH_START:
    KASSERT(request->capture && request->parent && !request->group);
    request->result = launcher_start(request->capture, request->parent,
        request->parent_cpu, request->execution_group,
        &request->reservation, request->slots, &request->child);
    discard_capture(request->capture);
    break;
  case LAUNCH_CREATE_SPACE:
    KASSERT(request->capture && request->parent && !request->group);
    request->result = launcher_create_space(request->capture, request->parent,
        &request->reservation, request->slots, &request->child);
    discard_capture(request->capture);
    break;
  case LAUNCH_BATCH_PREPARE:
    KASSERT(request->capture && request->group && request->parent);
    request->result = launcher_batch_prepare(request->group, request->capture,
        request->parent, request->parent_cpu, request->execution_group);
    discard_capture(request->capture);
    break;
  case LAUNCH_DISCARD:
    KASSERT(request->capture && !request->group && !request->parent);
    discard_capture(request->capture);
    break;
  case LAUNCH_BATCH_PUBLISH:
    KASSERT(request->group && !request->capture && !request->parent);
    request->result = launcher_batch_publish(request->group, request->children);
    launcher_batch_discard(request->group);
    break;
  case LAUNCH_BATCH_DISCARD:
    KASSERT(request->group && !request->capture && !request->parent);
    launcher_batch_discard(request->group);
    break;
  default:
    KASSERT(false);
  }
  capability_reservation_release(&request->reservation, request->slots);
  object_cleanup_leave(previous);
  request->capture = NULL;
  request->group = NULL;
  request->parent = NULL;
  request->execution_group = NULL;
}

static struct launcher_request *request_launch_service_reserved(enum launcher_action action,
    struct launch_capture *capture, struct launch_preparation *group,
    struct execution_group *execution_group, size_t grant_count,
    struct capability_reservation *reservation, struct capability_reserved_slot *slots)
{
  struct launcher_request *request =
      (struct launcher_request *)bsp_request_prepare(BSP_SERVICE_LAUNCHER);
  *request = (struct launcher_request){
    .request = request->request,
    .action = action,
    .capture = capture,
    .group = group,
    .execution_group = execution_group,
    .result = CALL_OK,
    .child = HANDLE_INVALID,
    .grant_count = grant_count,
  };
  if (reservation) {
    KASSERT(reservation->count <= 2);
    request->reservation = *reservation;
    memcpy(request->slots, slots, reservation->count * sizeof(*slots));
    *reservation = (struct capability_reservation){0};
  }
  if (action == LAUNCH_START || action == LAUNCH_BATCH_PREPARE ||
      action == LAUNCH_CREATE_SPACE || action == LAUNCH_CREATE_EXECUTION_GROUP) {
    request->parent = process_current();
    KASSERT(request->parent);
    request->parent_cpu = arch_cpu_index();
  }
  bsp_request_submit_and_wait(&request->request);
  return request;
}

static struct launcher_request *request_launch_service(enum launcher_action action,
    struct launch_capture *capture, struct launch_preparation *group,
    struct execution_group *execution_group)
{
  return request_launch_service_reserved(action, capture, group, execution_group, 0, NULL, NULL);
}

static struct launch_capture *allocate_launch_capture(size_t grant_count)
{
  struct launcher_request *request = request_launch_service_reserved(LAUNCH_ALLOCATE, NULL, NULL, NULL,
      grant_count, NULL, NULL);
  struct launch_capture *capture = request->capture_result;
  request->capture_result = NULL;
  bsp_request_release(&request->request);
  return capture;
}

static void discard_launch_capture(struct launch_capture *capture)
{
  struct launcher_request *request = request_launch_service(LAUNCH_DISCARD, capture, NULL, NULL);
  bsp_request_release(&request->request);
}

static enum call_status launch_process(struct launch_capture *capture,
    struct execution_group *execution_group, struct capability_reservation *reservation,
    struct capability_reserved_slot *slots, handle_t *child)
{
  struct launcher_request *request = request_launch_service_reserved(LAUNCH_START, capture,
      NULL, execution_group, 0, reservation, slots);
  *child = request->child;
  enum call_status result = request->result;
  bsp_request_release(&request->request);
  return result;
}

static struct launch_preparation *create_launch_batch(void)
{
  struct launcher_request *request = request_launch_service(LAUNCH_BATCH_CREATE, NULL, NULL, NULL);
  struct launch_preparation *group = request->group_result;
  request->group_result = NULL;
  bsp_request_release(&request->request);
  return group;
}

static enum call_status prepare_launch_batch(struct launch_preparation *group,
    struct launch_capture *capture, struct execution_group *execution_group)
{
  struct launcher_request *request = request_launch_service(LAUNCH_BATCH_PREPARE, capture, group, execution_group);
  enum call_status result = request->result;
  bsp_request_release(&request->request);
  return result;
}

static enum call_status publish_launch_batch(struct launch_preparation *group, handle_t *children)
{
  struct launcher_request *request = request_launch_service(LAUNCH_BATCH_PUBLISH, NULL, group, NULL);
  memcpy(children, request->children, sizeof(request->children));
  enum call_status status = request->result;
  bsp_request_release(&request->request);
  return status;
}

static void discard_launch_batch(struct launch_preparation *group)
{
  struct launcher_request *request = request_launch_service(LAUNCH_BATCH_DISCARD, NULL, group, NULL);
  bsp_request_release(&request->request);
}

/* Arrays use at most 8-byte alignment. All pointer/count arithmetic is bounded
 * by this one capture budget before user addresses are indexed. */
void *launcher_capture_array(struct launch_capture *capture, uintptr_t address,
    size_t count, size_t item_size)
{
  if (!count || capture->error != CALL_OK) {
    return NULL;
  }
  size_t offset = (capture->used + 7) & ~(size_t)7;
  if (offset > sizeof(capture->data) ||
      count > (sizeof(capture->data) - offset) / item_size) {
    capture->error = CALL_BAD_REQUEST;
    return NULL;
  }
  size_t bytes = count * item_size;
  void *array = capture->data + offset;
  if (!copy_from_user(array, address, bytes)) {
    capture->error = CALL_BAD_BUFFER;
    return NULL;
  }
  capture->used = offset + bytes;
  return array;
}

static const char *capture_string(struct launch_capture *capture, uintptr_t address)
{
  if (capture->error != CALL_OK) {
    return NULL;
  }
  char *text = (char *)capture->data + capture->used;
  size_t remaining = sizeof(capture->data) - capture->used;
  for (size_t length = 0; length < remaining; ++length) {
    if (length > UINTPTR_MAX - address ||
        !copy_from_user(text + length, address + length, 1)) {
      capture->error = CALL_BAD_BUFFER;
      return NULL;
    }
    if (!text[length]) {
      capture->used += length + 1;
      return text;
    }
  }
  capture->error = CALL_BAD_REQUEST;
  return NULL;
}

static struct process_binding *capture_bindings(struct launch_capture *capture,
    uintptr_t address, size_t count)
{
  /* The wire record and kernel record have equal size/alignment on x86_64.
   * Copy each through a local wire value before replacing it with pointers;
   * never dereference a caller-supplied address as a kernel string. */
  _Static_assert(sizeof(struct launch_binding) == sizeof(struct process_binding),
                 "binding capture storage");
  struct process_binding *bindings = launcher_capture_array(capture, address, count, sizeof(*bindings));
  if (!bindings) {
    return NULL;
  }
  for (size_t i = 0; i < count && capture->error == CALL_OK; ++i) {
    struct launch_binding wire;
    memcpy(&wire, &bindings[i], sizeof(wire));
    if (wire.grant >= capture->grant_count) {
      capture->error = CALL_BAD_REQUEST;
      break;
    }
    bindings[i].name = capture_string(capture, wire.name);
    bindings[i].handle = wire.grant;
  }
  return bindings;
}

static void capture_streams(struct launch_capture *capture,
                            const struct launch_request *source)
{
  if (capture->error != CALL_OK) {
    return;
  }
  struct process_startup *startup = &capture->startup;
  for (size_t i = 0; i < STARTUP_STREAM_COUNT; ++i) {
    const struct launch_stream *stream = &source->streams[i];
    if (stream->protocol == STARTUP_STREAM_NONE) {
      if (stream->grant != 0) {
        capture->error = CALL_BAD_REQUEST;
        return;
      }
      continue;
    }

    uint64_t rights;
    if (stream->protocol == PROTOCOL_CONSOLE) {
      rights = i == STARTUP_STDIN ? CONSOLE_RIGHT_READ : CONSOLE_RIGHT_WRITE;
    } else if (stream->protocol == PROTOCOL_FILE) {
      rights = i == STARTUP_STDIN ? FILE_RIGHT_READ : FILE_RIGHT_WRITE;
    } else if (stream->protocol == PROTOCOL_PIPE) {
      rights = i == STARTUP_STDIN ? PIPE_RIGHT_READ : PIPE_RIGHT_WRITE;
    } else {
      capture->error = CALL_BAD_REQUEST;
      return;
    }

    if (stream->grant >= capture->grant_count ||
        capture->grants[stream->grant].rights != rights) {
      capture->error = CALL_BAD_REQUEST;
      return;
    }
    for (size_t j = 0; j < i; ++j) {
      if (startup->streams[j].protocol != STARTUP_STREAM_NONE &&
          stream->grant == startup->streams[j].handle) {
        capture->error = CALL_BAD_REQUEST;
        return;
      }
    }
    for (size_t j = 0; j < startup->resource_count; ++j) {
      if (stream->grant == startup->resources[j].handle) {
        capture->error = CALL_BAD_REQUEST;
        return;
      }
    }
    for (size_t j = 0; j < startup->root_count; ++j) {
      if (stream->grant == startup->roots[j].handle) {
        capture->error = CALL_BAD_REQUEST;
        return;
      }
    }
    for (size_t j = 0; j < startup->working_directory_count; ++j) {
      if (stream->grant == startup->working_directories[j]) {
        capture->error = CALL_BAD_REQUEST;
        return;
      }
    }
    if (startup->namespace && stream->grant == startup->namespace - 1) {
      capture->error = CALL_BAD_REQUEST;
      return;
    }

    bool valid = object_stream_valid(capture->owned_grants[stream->grant].object,
        stream->protocol, capture->grants[stream->grant].transport);
    if (!valid) {
      capture->error = CALL_WRONG_TYPE;
      return;
    }
    startup->streams[i] = (struct startup_stream){stream->protocol, stream->grant};
  }
}

static void capture_namespace(struct launch_capture *capture, uint64_t namespace_grant)
{
  if (!namespace_grant || capture->error != CALL_OK) {
    return;
  }
  uint64_t index = namespace_grant - 1;
  if (index >= capture->grant_count) {
    capture->error = CALL_BAD_REQUEST;
    return;
  }

  const struct launch_grant *grant = &capture->grants[index];
  if (!(grant->rights & NAMESPACE_RIGHT_LOOKUP) || grant->transport) {
    capture->error = CALL_BAD_REQUEST;
    return;
  }
  bool valid = capture->owned_grants[index].object->type == OBJECT_NAMESPACE;
  if (!valid) {
    capture->error = CALL_WRONG_TYPE;
    return;
  }
  capture->startup.namespace = namespace_grant;
}

static void capture_startup(struct launch_capture *capture, const struct launch_request *source)
{
  capture->grants = launcher_capture_array(capture, source->grants, source->grant_count,
      sizeof(*capture->grants));
  for (size_t i = 0; i < capture->grant_count && capture->error == CALL_OK; ++i) {
    const struct launch_grant *grant = &capture->grants[i];
    enum capability_result status = capability_grant_acquire(&process_current()->capabilities,
        grant->source, grant->rights, grant->transport, &capture->owned_grants[i]);
    if (status != CAP_OK) {
      capture->error = status == CAP_LIMIT ? CALL_LIMIT :
          status == CAP_BAD_HANDLE ? CALL_BAD_HANDLE :
          status == CAP_DENIED ? CALL_DENIED : CALL_BAD_REQUEST;
    }
  }
  struct process_startup *startup = &capture->startup;
  startup->resource_count = source->resource_count;
  startup->resources = capture_bindings(capture, source->resources, source->resource_count);
  startup->root_count = source->root_count;
  startup->roots = capture_bindings(capture, source->roots, source->root_count);
  startup->working_directory_count = source->working_directory_count;
  startup->working_directories = launcher_capture_array(capture, source->working_directories,
      source->working_directory_count, sizeof(handle_t));
  for (size_t i = 0; i < source->working_directory_count && capture->error == CALL_OK; ++i) {
    if (startup->working_directories[i] >= source->grant_count) {
      capture->error = CALL_BAD_REQUEST;
    }
  }
  capture_namespace(capture, source->namespace_grant);
  capture_streams(capture, source);
  if (source->working_path) {
    startup->working_path = capture_string(capture, source->working_path);
  }

  _Static_assert(sizeof(struct startup_variable) == sizeof(struct process_variable),
                 "environment capture storage");
  startup->environment_count = source->environment_count;
  struct process_variable *environment = launcher_capture_array(capture, source->environment,
      source->environment_count, sizeof(*environment));
  startup->environment = environment;
  for (size_t i = 0; i < source->environment_count && capture->error == CALL_OK; ++i) {
    struct startup_variable wire;
    memcpy(&wire, &environment[i], sizeof(wire));
    environment[i].name = capture_string(capture, wire.name);
    environment[i].value = capture_string(capture, wire.value);
  }

  startup->argc = source->argc;
  const char **argv = launcher_capture_array(capture, source->argv, source->argc, sizeof(*argv));
  startup->argv = argv;
  for (size_t i = 0; i < source->argc && capture->error == CALL_OK; ++i) {
    argv[i] = capture_string(capture, (uintptr_t)argv[i]);
  }
}

enum call_status launcher_capture_request(const struct launch_request *request,
    struct launch_capture **result)
{
  *result = NULL;
  if (request->grant_count > LAUNCH_CAPTURE_MAX_SIZE / sizeof(struct launch_grant)) {
    return CALL_BAD_REQUEST;
  }
  size_t initial_stack_bytes;
  if (user_initial_stack_size(request->initial_stack_bytes, &initial_stack_bytes) != MM_OK) {
    return CALL_BAD_REQUEST;
  }
  struct capability_reference image;
  enum capability_result lookup = capability_acquire(&process_current()->capabilities,
      request->image, FILE_RIGHT_READ, 0, &image);
  if (lookup != CAP_OK) {
    return lookup == CAP_LIMIT ? CALL_LIMIT :
        lookup == CAP_BAD_HANDLE ? CALL_BAD_HANDLE : CALL_DENIED;
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
  uint64_t image_rights = image.rights;
  capture->image = (struct file_object *)image.object;
  image.object = NULL;
  capture->initial_stack_bytes = initial_stack_bytes;
  if (task_stop_requested()) {
    discard_launch_capture(capture);
    return CALL_ENDPOINT_CLOSED;
  }
  capture_startup(capture, request);
  if (capture->error != CALL_OK) {
    enum call_status error = capture->error;
    discard_launch_capture(capture);
    return error;
  }
  if (capture->image->backing == FILE_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_CAPTURE);
    pending->job.node = capture->image->npfs;
    pending->job.rights = image_rights;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    capture->captured_image = pending->job.captured;
    pending->job.captured = (struct image_capture){0};
    npfs_request_release(pending);
    if (status != CALL_OK) {
      discard_launch_capture(capture);
      return status;
    }
  } else if (capture->image->backing == FILE_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_CAPTURE);
    pending->node = capture->image->host;
    hostfs_request_submit_and_wait(pending);
    if (pending->status != CALL_OK) {
      enum call_status error = pending->status;
      hostfs_request_release(pending);
      discard_launch_capture(capture);
      return error;
    }
    capture->captured_image = pending->captured;
    pending->captured = (struct image_capture){0};
    hostfs_request_release(pending);
  } else if (!file_begin_operation(capture->image)) {
    discard_launch_capture(capture);
    return CALL_ENDPOINT_CLOSED;
  } else if (capture->image->backing == FILE_RAM) {
    /* Lend the stable file operation. Copy and mapping access stay on the BSP. */
    struct launcher_request *pending = request_launch_service(LAUNCH_CAPTURE_RAM,
        capture, NULL, NULL);
    enum call_status status = pending->result;
    bsp_request_release(&pending->request);
    if (status != CALL_OK) {
      discard_launch_capture(capture);
      return status;
    }
  }
  if (task_stop_requested()) {
    if (capture->image->backing == FILE_INITRD) {
      file_end_operation(capture->image);
    }
    discard_launch_capture(capture);
    return CALL_ENDPOINT_CLOSED;
  }

  *result = capture;
  return CALL_OK;
}

struct launch_capture *launcher_capture_empty(void)
{
  return allocate_launch_capture(0);
}

void launcher_capture_discard(struct launch_capture *capture)
{
  if (capture->image && capture->image->backing == FILE_INITRD) {
    file_end_operation(capture->image);
  }
  discard_launch_capture(capture);
}

enum call_status launcher_submit_space(struct launch_capture *capture, handle_t *child)
{
  *child = HANDLE_INVALID;
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (capture->image) {
    enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
    if (reserved != CAP_OK) {
      launcher_capture_discard(capture);
      return reserved == CAP_LIMIT ? CALL_LIMIT : CALL_NO_MEMORY;
    }
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    launcher_capture_discard(capture);
    return CALL_ENDPOINT_CLOSED;
  }
  struct launcher_request *request = request_launch_service_reserved(LAUNCH_CREATE_SPACE,
      capture, NULL, NULL, 0, &reservation, &slot);
  *child = request->child;
  enum call_status result = request->result;
  bsp_request_release(&request->request);
  return result;
}

static struct syscall_result launch_one(struct execution_group *execution_group,
    uintptr_t request_address, size_t request_size,
                                        uintptr_t reply_address, size_t reply_capacity)
{
  struct launch_request request;
  if (request_size != sizeof(request) || reply_capacity < sizeof(handle_t)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request)) ||
      !user_buffer_check(reply_address, sizeof(handle_t), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
  if (reserved != CAP_OK) {
    return (struct syscall_result){reserved == CAP_LIMIT ? CALL_LIMIT : CALL_NO_MEMORY, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct launch_capture *capture;
  enum call_status status = launcher_capture_request(&request, &capture);
  if (status != CALL_OK) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){status, 0};
  }

  /* Captured grants and the observer reservation survive source CLOSE and waits.
   * The stable image operation ends before child publication. */
  handle_t child;
  status = launch_process(capture, execution_group, &reservation, &slot, &child);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &child, sizeof(child)));
  return (struct syscall_result){CALL_OK, sizeof(child)};
}

static struct syscall_result batch_reply(uintptr_t address,
    const struct launch_batch_reply *reply, enum call_status status)
{
  KASSERT(copy_to_user(address, reply, sizeof(*reply)));
  return (struct syscall_result){status, sizeof(*reply)};
}

static struct syscall_result launch_batch(struct execution_group *execution_group,
    uintptr_t request_address, size_t request_size,
                                          uintptr_t reply_address, size_t reply_capacity)
{
  if (reply_capacity < sizeof(struct launch_batch_reply) ||
      !user_buffer_check(reply_address, sizeof(struct launch_batch_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct launch_batch_reply reply = {.failed_index = LAUNCH_NO_STAGE};
  if (request_size != sizeof(struct launch_batch_request)) {
    return batch_reply(reply_address, &reply, CALL_BAD_REQUEST);
  }
  struct launch_batch_request batch;
  if (!copy_from_user(&batch, request_address, sizeof(batch))) {
    return batch_reply(reply_address, &reply, CALL_BAD_BUFFER);
  }
  if (!batch.count || batch.count > LAUNCH_BATCH_MAX) {
    return batch_reply(reply_address, &reply, CALL_BAD_REQUEST);
  }

  struct launch_request requests[LAUNCH_BATCH_MAX];
  if (!copy_from_user(requests, batch.requests,
      batch.count * sizeof(*requests))) {
    return batch_reply(reply_address, &reply, CALL_BAD_BUFFER);
  }

  struct launch_preparation *group = create_launch_batch();
  if (!group) {
    return batch_reply(reply_address, &reply, CALL_NO_MEMORY);
  }

  enum capability_result reserved = launcher_batch_reserve(group, batch.count);
  if (reserved != CAP_OK) {
    discard_launch_batch(group);
    return batch_reply(reply_address, &reply,
        reserved == CAP_LIMIT ? CALL_LIMIT : CALL_NO_MEMORY);
  }
  if (task_stop_requested()) {
    discard_launch_batch(group);
    return batch_reply(reply_address, &reply, CALL_ENDPOINT_CLOSED);
  }
  for (size_t i = 0; i < batch.count; ++i) {
    struct launch_capture *capture;
    enum call_status status = launcher_capture_request(&requests[i], &capture);
    if (status == CALL_OK) {
      /* The BSP releases this stage's image operation before the next stage
       * acquires one, including repeated reads of the same file object. */
      status = prepare_launch_batch(group, capture, execution_group);
    }
    if (status != CALL_OK) {
      reply.failed_index = i;
      discard_launch_batch(group);
      return batch_reply(reply_address, &reply, status);
    }
  }

  enum call_status status = publish_launch_batch(group, reply.children);
  return batch_reply(reply_address, &reply, status);
}

static struct syscall_result create_group(size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
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
    return (struct syscall_result){reserved == CAP_LIMIT ? CALL_LIMIT : CALL_NO_MEMORY, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct launcher_request *request = request_launch_service_reserved(
      LAUNCH_CREATE_EXECUTION_GROUP, NULL, NULL, NULL, 0, &reservation, slots);
  enum call_status status = request->result;
  struct execution_group_create_reply reply = request->execution_reply;
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result launcher_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct process *parent = process_current();
  struct execution_group *group = launcher_execution_group(object);
  if (operation == LAUNCHER_CREATE_GROUP) {
    if (!(rights & LAUNCHER_RIGHT_CREATE_GROUP) || group || parent->execution_group) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return create_group(request_size, reply_address, reply_capacity);
  }
  if (operation != LAUNCHER_LAUNCH && operation != LAUNCHER_LAUNCH_BATCH) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & LAUNCHER_RIGHT_LAUNCH) ||
      (parent->execution_group && parent->execution_group != group)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  /* A process never changes space. Sealing can change while capture sleeps:
   * the BSP checks again at preparation and under the group lock at publication. */
  if (group && group->space != parent->space) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  enum call_status status = execution_group_launch_begin(group, parent->space);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct syscall_result result = operation == LAUNCHER_LAUNCH_BATCH ?
      launch_batch(group, request_address, request_size, reply_address, reply_capacity) :
      launch_one(group, request_address, request_size, reply_address, reply_capacity);
  execution_group_launch_end(group);
  return result;
}
