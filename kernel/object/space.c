#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/space.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
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

struct syscall_result space_control_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size)
{
  struct space_control *control = (struct space_control *)object;
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
