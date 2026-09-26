#include <abi/file.h>
#include <abi/console.h>
#include <abi/pipe.h>
#include <arch/smp.h>
#include <kernel/fs/hostfs.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void destroy_launcher(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *launcher_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_LAUNCHER, destroy_launcher);
  }
  return object;
}

/* Arrays use at most 8-byte alignment. All pointer/count arithmetic is bounded
 * by this one capture budget before user addresses are indexed. */
static void *capture_array(struct launch_capture *capture, uintptr_t address,
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
  struct process_binding *bindings = capture_array(capture, address, count, sizeof(*bindings));
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

    enum object_type type;
    uint64_t rights;
    if (stream->protocol == PROTOCOL_CONSOLE) {
      type = OBJECT_CONSOLE;
      rights = i == STARTUP_STDIN ? CONSOLE_RIGHT_READ : CONSOLE_RIGHT_WRITE;
    } else if (stream->protocol == PROTOCOL_FILE) {
      type = OBJECT_FILE;
      rights = i == STARTUP_STDIN ? FILE_RIGHT_READ : FILE_RIGHT_WRITE;
    } else if (stream->protocol == PROTOCOL_PIPE) {
      type = OBJECT_PIPE;
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

    struct kernel_object *object;
    enum capability_result found = capability_resolve(&process_current()->capabilities,
        capture->grants[stream->grant].source, rights, &object, NULL);
    if (found != CAP_OK) {
      capture->error = found == CAP_BAD_HANDLE ? CALL_BAD_HANDLE : CALL_DENIED;
      return;
    }
    if (object->type != type) {
      capture->error = CALL_WRONG_TYPE;
      return;
    }
    startup->streams[i] = (struct startup_stream){stream->protocol, stream->grant};
  }
}

static void capture_startup(struct launch_capture *capture, const struct launch_request *source)
{
  capture->grant_count = source->grant_count;
  capture->grants = capture_array(capture, source->grants, source->grant_count,
      sizeof(*capture->grants));
  struct process_startup *startup = &capture->startup;
  startup->resource_count = source->resource_count;
  startup->resources = capture_bindings(capture, source->resources, source->resource_count);
  startup->root_count = source->root_count;
  startup->roots = capture_bindings(capture, source->roots, source->root_count);
  startup->working_directory_count = source->working_directory_count;
  startup->working_directories = capture_array(capture, source->working_directories,
      source->working_directory_count, sizeof(handle_t));
  for (size_t i = 0; i < source->working_directory_count && capture->error == CALL_OK; ++i) {
    if (startup->working_directories[i] >= source->grant_count) {
      capture->error = CALL_BAD_REQUEST;
    }
  }
  capture_streams(capture, source);
  if (source->working_path) {
    startup->working_path = capture_string(capture, source->working_path);
  }

  _Static_assert(sizeof(struct startup_variable) == sizeof(struct process_variable),
                 "environment capture storage");
  startup->environment_count = source->environment_count;
  struct process_variable *environment = capture_array(capture, source->environment,
      source->environment_count, sizeof(*environment));
  startup->environment = environment;
  for (size_t i = 0; i < source->environment_count && capture->error == CALL_OK; ++i) {
    struct startup_variable wire;
    memcpy(&wire, &environment[i], sizeof(wire));
    environment[i].name = capture_string(capture, wire.name);
    environment[i].value = capture_string(capture, wire.value);
  }

  startup->argc = source->argc;
  const char **argv = capture_array(capture, source->argv, source->argc, sizeof(*argv));
  startup->argv = argv;
  for (size_t i = 0; i < source->argc && capture->error == CALL_OK; ++i) {
    argv[i] = capture_string(capture, (uintptr_t)argv[i]);
  }
}

static enum call_status capture_launch_request(const struct launch_request *request,
                                               struct launch_capture **result)
{
  *result = NULL;
  struct kernel_object *image;
  enum capability_result lookup = capability_resolve(&process_current()->capabilities,
      request->image, FILE_RIGHT_READ, &image, NULL);
  if (lookup != CAP_OK) {
    return lookup == CAP_BAD_HANDLE ? CALL_BAD_HANDLE : CALL_DENIED;
  }
  if (image->type != OBJECT_FILE) {
    return CALL_WRONG_TYPE;
  }

  struct launch_capture *capture = task_allocate_launch_capture();
  if (!capture) {
    return CALL_NO_MEMORY;
  }
  capture_startup(capture, request);
  if (capture->error != CALL_OK) {
    enum call_status error = capture->error;
    task_discard_launch_capture(capture);
    return error;
  }
  capture->image = (struct file_object *)image;
  if (capture->image->backing == FILE_HOST) {
    struct hostfs_request *pending = task_prepare_hostfs();
    pending->operation = HOSTFS_CAPTURE;
    pending->node = capture->image->host;
    pending->count = LAUNCH_HOST_IMAGE_MAX_SIZE;
    task_submit_hostfs(pending);
    if (pending->status != CALL_OK) {
      enum call_status error = pending->status;
      task_discard_launch_capture(capture);
      return error;
    }
    capture->host_image = pending->captured;
    capture->host_image_size = pending->count;
    pending->captured = NULL;
  } else {
    file_begin_operation(capture->image);
  }

  *result = capture;
  return CALL_OK;
}

static struct syscall_result launch_one(uintptr_t request_address, size_t request_size,
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

  struct launch_capture *capture;
  enum call_status status = capture_launch_request(&request, &capture);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }

  /* No other task can close source handles or mutate caller mappings. The BSP
   * borrows the table and stable image, releases any file operation before
   * child submission, and frees staging before waking this caller. */
  handle_t child;
  status = task_launch_process(capture, &child);
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

static struct syscall_result launch_batch(uintptr_t request_address, size_t request_size,
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

  struct launch_group *group = task_create_launch_group();
  if (!group) {
    return batch_reply(reply_address, &reply, CALL_NO_MEMORY);
  }

  for (size_t i = 0; i < batch.count; ++i) {
    struct launch_capture *capture;
    enum call_status status = capture_launch_request(&requests[i], &capture);
    if (status == CALL_OK) {
      /* The BSP releases this stage's image operation before the next stage
       * acquires one, including repeated reads of the same file object. */
      status = task_prepare_launch_group(group, capture);
    }
    if (status != CALL_OK) {
      reply.failed_index = i;
      task_discard_launch_group(group);
      return batch_reply(reply_address, &reply, status);
    }
  }

  task_publish_launch_group(group, reply.children);
  return batch_reply(reply_address, &reply, CALL_OK);
}

struct syscall_result launcher_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != LAUNCHER_LAUNCH && operation != LAUNCHER_LAUNCH_BATCH) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & LAUNCHER_RIGHT_LAUNCH)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == LAUNCHER_LAUNCH_BATCH) {
    return launch_batch(request_address, request_size, reply_address, reply_capacity);
  }
  return launch_one(request_address, request_size, reply_address, reply_capacity);
}
