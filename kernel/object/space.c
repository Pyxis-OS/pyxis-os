#include <abi/launcher.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/file.h>
#include <kernel/object/space.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/string.h>
#include <kernel/memory.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

#include "launcher_internal.h"

struct space_control {
  struct kernel_object object;
  struct space *space; /* Borrowed; spaces survive all processes and handles. */
};

static void destroy_space_control(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *space_control_create(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  struct space_control *control = kmalloc(sizeof(*control));
  if (!control) {
    return NULL;
  }
  *control = (struct space_control){.space = space};
  object_init(&control->object, OBJECT_SPACE, destroy_space_control);
  return &control->object;
}

static struct syscall_result set_affinity(struct space *space, uintptr_t request_address,
    size_t request_size)
{
  struct { uint64_t cpus, cpu_count; } request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t count = arch_cpu_count();
  if (!request.cpu_count || request.cpu_count > count) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  /* Once setup closes, delegated grants can let several of the space's tasks
   * call at once, so refuse before touching the shared staging bitmap. While
   * it is open the caller is the space's only task, which alone could close
   * it, so staging needs no lock. The commit checks again under the queue lock. */
  if (!task_space_setup_open(space)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  uint64_t *staging = space->affinity_staging;
  size_t words = (request.cpu_count + 63) / 64;
  memset(staging, 0, space_cpu_words() * sizeof(*staging));
  if (!copy_from_user(staging, request.cpus, words * sizeof(*staging))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.cpu_count % 64 && staging[words - 1] >> (request.cpu_count % 64)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  bool any = false, outside = false;
  for (size_t cpu = 0; cpu < request.cpu_count; ++cpu) {
    if (!((staging[cpu / 64] >> (cpu % 64)) & 1)) {
      continue;
    }
    any = true;
    outside |= !space_ceiling_allows(space, cpu);
  }
  if (!any) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (outside) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  return (struct syscall_result){task_space_set_affinity(space, staging), 0};
}

struct syscall_result space_control_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size)
{
  struct space_control *control = (struct space_control *)object;
  if (operation == SPACE_SET_AFFINITY) {
    if (!(rights & SPACE_RIGHT_SET_AFFINITY) || process_current()->space != control->space) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return set_affinity(control->space, request_address, request_size);
  }
  if (operation != SPACE_SET_TITLE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & SPACE_RIGHT_SET_TITLE) || process_current()->space != control->space) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct { uint64_t title, length; } request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!request.length || request.length > SPACE_TITLE_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  char title[SPACE_TITLE_MAX + 1];
  if (!copy_from_user(title, request.title, request.length)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!space_set_title(control->space, title, request.length)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  return (struct syscall_result){CALL_OK, 0};
}

static void destroy_space_factory(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *space_factory_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *factory = kmalloc(sizeof(*factory));
  if (!factory) {
    return NULL;
  }
  object_init(factory, OBJECT_SPACE_FACTORY, destroy_space_factory);
  return factory;
}

/* Copies 1..MAX bytes of printable ASCII into TEXT and terminates it. */
static enum call_status copy_text(uint64_t address, uint64_t length, size_t max, char *text)
{
  if (!length || length > max) {
    return CALL_BAD_REQUEST;
  }
  if (!copy_from_user(text, address, length)) {
    return CALL_BAD_BUFFER;
  }
  for (size_t i = 0; i < length; ++i) {
    if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e) {
      return CALL_BAD_REQUEST;
    }
  }
  text[length] = '\0';
  return CALL_OK;
}

/* Names the kernel adds to a new space's first process. */
static bool reserved_resource(const char *name)
{
  static const char *const reserved[] = {
    "input", "output", "keyboard", "pointer", "display", "audio", "space",
  };
  size_t length = strlen(name);
  for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
    if (strlen(reserved[i]) == length && !memcmp(reserved[i], name, length)) {
      return true;
    }
  }
  return false;
}

/* Launch path: captures the request and the ceiling into CAPTURE's storage. */
static enum call_status capture_launch(uint64_t launch_address, uint64_t cpus,
    uint64_t cpu_count, struct launch_capture **result)
{
  *result = NULL;
  if (!cpu_count || cpu_count > arch_cpu_count()) {
    return CALL_BAD_REQUEST;
  }
  struct launch_request launch;
  if (!copy_from_user(&launch, launch_address, sizeof(launch))) {
    return CALL_BAD_BUFFER;
  }
  for (size_t i = 0; i < STARTUP_STREAM_COUNT; ++i) {
    if (launch.streams[i].protocol != STARTUP_STREAM_NONE) {
      return CALL_BAD_REQUEST;
    }
  }
  struct launch_capture *capture;
  enum call_status status = launcher_capture_request(&launch, &capture);
  if (status != CALL_OK) {
    return status;
  }
  for (size_t i = 0; i < capture->startup.resource_count; ++i) {
    if (reserved_resource(capture->startup.resources[i].name)) {
      launcher_capture_discard(capture);
      return CALL_BAD_REQUEST;
    }
  }
  size_t words = (cpu_count + 63) / 64;
  const uint64_t *set = launcher_capture_array(capture, cpus, words, sizeof(uint64_t));
  if (!set) {
    status = capture->error;
    launcher_capture_discard(capture);
    return status;
  }
  bool any = false;
  for (size_t i = 0; i < words; ++i) {
    any |= set[i] != 0;
  }
  if (!any || (cpu_count % 64 && set[words - 1] >> (cpu_count % 64))) {
    launcher_capture_discard(capture);
    return CALL_BAD_REQUEST;
  }
  capture->space.cpus = set;
  capture->space.cpu_count = cpu_count;
  *result = capture;
  return CALL_OK;
}

struct syscall_result space_factory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != SPACE_FACTORY_CREATE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & SPACE_FACTORY_RIGHT_CREATE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct {
    uint64_t name, name_length, title, title_length;
    uint64_t cpus, cpu_count, reason, reason_length, launch;
  } request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  char name[SPACE_NAME_MAX + 1], title[SPACE_TITLE_MAX + 1], reason[SPACE_REASON_MAX + 1];
  enum call_status status = copy_text(request.name, request.name_length, SPACE_NAME_MAX, name);
  if (status == CALL_OK && !space_name_valid(name, request.name_length)) {
    status = CALL_BAD_REQUEST;
  }
  if (status == CALL_OK) {
    status = copy_text(request.title, request.title_length, SPACE_TITLE_MAX, title);
  }
  bool unstarted = request.reason_length != 0;
  if (status == CALL_OK && unstarted == (request.launch != 0)) {
    status = CALL_BAD_REQUEST;
  }
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }

  struct launch_capture *capture;
  if (unstarted) {
    status = copy_text(request.reason, request.reason_length, SPACE_REASON_MAX, reason);
    if (status == CALL_OK && request.cpu_count) {
      status = CALL_BAD_REQUEST;
    }
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
    capture = launcher_capture_empty();
    if (!capture) {
      return (struct syscall_result){CALL_NO_MEMORY, 0};
    }
    memcpy(capture->space.reason, reason, request.reason_length + 1);
  } else {
    if (reply_capacity < sizeof(handle_t)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(handle_t), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    status = capture_launch(request.launch, request.cpus, request.cpu_count, &capture);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
  }
  memcpy(capture->space.name, name, request.name_length + 1);
  memcpy(capture->space.title, title, request.title_length + 1);

  handle_t child;
  status = launcher_submit_space(capture, &child);
  if (status != CALL_OK || unstarted) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &child, sizeof(child)));
  return (struct syscall_result){CALL_OK, sizeof(child)};
}
