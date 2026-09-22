#include <abi/file.h>
#include <arch/smp.h>
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

struct syscall_result launcher_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != LAUNCHER_LAUNCH) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & LAUNCHER_RIGHT_LAUNCH)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct launch_request request;
  if (request_size != sizeof(request) || reply_capacity < sizeof(handle_t)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request)) ||
      !user_buffer_check(reply_address, sizeof(handle_t), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct kernel_object *image;
  enum capability_result lookup = capability_resolve(&process_current()->capabilities,
      request.image, FILE_RIGHT_READ, &image, NULL);
  if (lookup != CAP_OK) {
    return (struct syscall_result){lookup == CAP_BAD_HANDLE ? CALL_BAD_HANDLE : CALL_DENIED, 0};
  }
  if (image->type != OBJECT_FILE) {
    return (struct syscall_result){CALL_WRONG_TYPE, 0};
  }

  struct launch_capture *capture = task_allocate_launch_capture();
  if (!capture) {
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  }
  capture_startup(capture, &request);
  if (capture->error != CALL_OK) {
    enum call_status error = capture->error;
    task_discard_launch_capture(capture);
    return (struct syscall_result){error, 0};
  }
  capture->image = (struct file_object *)image;
  file_begin_operation(capture->image);

  /* No other task can close source handles or mutate caller mappings. The BSP
   * borrows the table and file operation, releases the operation before child
   * submission, and frees capture storage before waking this caller. */
  handle_t child;
  enum call_status status = task_launch_process(capture, &child);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &child, sizeof(child)));
  return (struct syscall_result){CALL_OK, sizeof(child)};
}
