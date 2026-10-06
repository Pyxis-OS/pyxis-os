#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/space.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/memory.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

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
