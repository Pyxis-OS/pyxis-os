#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/user.h>

/* IF=0; group -> scheduler queues. Never allocate, copy user memory or switch
 * context while held. Final grant release may run on any CPU. */
static void lock_group(struct execution_group *group)
{
  while (atomic_exchange_explicit(&group->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_group(struct execution_group *group)
{
  atomic_store_explicit(&group->locked, false, memory_order_release);
}

static void destroy_group(struct kernel_object *object)
{
  struct execution_group *group = (struct execution_group *)object;
  KASSERT(!group->controllers && !group->members);
  kfree(group);
}

struct execution_group *execution_group_create(struct space *space, size_t cpu_index)
{
  KASSERT(arch_cpu_index() == 0 && space);
  struct execution_group *group = kmalloc(sizeof(*group));
  if (group) {
    *group = (struct execution_group){.space = space, .cpu_index = cpu_index};
    atomic_init(&group->locked, false);
    object_init(&group->object, OBJECT_EXECUTION_GROUP, destroy_group);
  }
  return group;
}

enum call_status execution_group_check(struct execution_group *group,
    struct space *space, size_t cpu_index)
{
  if (!group) {
    return CALL_OK;
  }
  if (group->space != space || group->cpu_index != cpu_index) {
    return CALL_DENIED;
  }
  lock_group(group);
  bool sealed = group->sealed;
  unlock_group(group);
  return sealed ? CALL_ENDPOINT_CLOSED : CALL_OK;
}

enum call_status execution_group_publish(struct execution_group *group,
    struct task **tasks, size_t count)
{
  KASSERT(arch_cpu_index() == 0);
  if (!group) {
    user_task_publish_group(tasks, count);
    return CALL_OK;
  }
  lock_group(group);
  enum call_status status = CALL_OK;
  if (group->sealed) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (count > SIZE_MAX - group->members) {
    status = CALL_LIMIT;
  } else {
    group->members += count;
    user_task_publish_group(tasks, count);
  }
  unlock_group(group);
  return status;
}

void execution_group_member_complete(struct execution_group *group)
{
  KASSERT(arch_cpu_index() == 0);
  lock_group(group);
  KASSERT(group->members);
  --group->members;
  unlock_group(group);
  object_release(&group->object);
}

bool execution_group_authority_retain(struct kernel_object *object, uint64_t rights)
{
  if (object->type != OBJECT_EXECUTION_GROUP || !(rights & EXECUTION_GROUP_RIGHT_CONTROL)) {
    return true;
  }
  struct execution_group *group = (struct execution_group *)object;
  lock_group(group);
  bool retained = group->controllers != SIZE_MAX;
  if (retained) {
    ++group->controllers;
  }
  unlock_group(group);
  return retained;
}

void execution_group_authority_release(struct kernel_object *object, uint64_t rights)
{
  if (object->type != OBJECT_EXECUTION_GROUP || !(rights & EXECUTION_GROUP_RIGHT_CONTROL)) {
    return;
  }
  struct execution_group *group = (struct execution_group *)object;
  lock_group(group);
  KASSERT(group->controllers);
  if (--group->controllers == 0) {
    group->sealed = true;
  }
  unlock_group(group);
}

struct syscall_result execution_group_call(struct execution_group *group,
    uint64_t rights, uint64_t operation, size_t request_size)
{
  if (operation != EXECUTION_GROUP_SEAL) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & EXECUTION_GROUP_RIGHT_CONTROL)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  lock_group(group);
  group->sealed = true;
  unlock_group(group);
  return (struct syscall_result){CALL_OK, 0};
}
