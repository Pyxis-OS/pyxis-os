#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/profile.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void destroy_profile(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *profile_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_PROFILE, destroy_profile);
  }
  return object;
}

struct syscall_result profile_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != PROFILE_BEGIN && operation != PROFILE_SNAPSHOT && operation != PROFILE_END) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & PROFILE_RIGHT_MEMORY)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct profile_snapshot reply;
  bool returns_snapshot = operation != PROFILE_BEGIN;
  if (request_size || (returns_snapshot && reply_capacity < sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (returns_snapshot && !user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  enum call_status status = task_profile_control(operation, &reply);
  if (status != CALL_OK || !returns_snapshot) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
