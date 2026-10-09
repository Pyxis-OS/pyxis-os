#include <kernel/object/clipboard.h>
#include <abi/wait.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user.h>
#include <kernel/user/wait.h>
#include <kernel/wait.h>

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

static bool complete_locked(struct execution_group *group)
{
  if (group->complete || !group->sealed || group->members ||
      group->launches || group->cleanup_pending) {
    return false;
  }
  group->complete = true;
  while (group->waiters) {
    struct task_wait_link *waiter = group->waiters;
    group->waiters = waiter->next;
    waiter->next = NULL;
    struct task_wait *wait = waiter->wait;
    waiter->wait = NULL;
    task_wait_wake(wait);
  }
  return true;
}

static void stop_locked(struct execution_group *group)
{
  group->sealed = true;
  if (!group->stopping) {
    group->stopping = true;
    for (struct execution_group_member *member = group->first_member;
         member; member = member->next) {
      task_request_stop(member->task);
    }
  }
  complete_locked(group);
}

static void destroy_group(struct kernel_object *object)
{
  struct execution_group *group = (struct execution_group *)object;
  KASSERT(!group->controllers && !group->members && !group->launches &&
      !group->cleanup_pending && !group->first_member && !group->waiters);
  kfree(group);
}

struct execution_group *execution_group_create(struct space *space)
{
  KASSERT(arch_cpu_index() == 0 && space);
  struct execution_group *group = kmalloc(sizeof(*group));
  if (group) {
    *group = (struct execution_group){.space = space};
    atomic_init(&group->locked, false);
    object_init(&group->object, OBJECT_EXECUTION_GROUP, destroy_group);
  }
  return group;
}

enum call_status execution_group_check(struct execution_group *group, struct space *space)
{
  if (!group) {
    return CALL_OK;
  }
  if (group->space != space) {
    return CALL_DENIED;
  }
  lock_group(group);
  bool sealed = group->sealed;
  unlock_group(group);
  return sealed ? CALL_ENDPOINT_CLOSED : CALL_OK;
}

enum call_status execution_group_launch_begin(struct execution_group *group,
    struct space *space)
{
  if (!group) {
    return CALL_OK;
  }
  if (group->space != space) {
    return CALL_DENIED;
  }
  lock_group(group);
  enum call_status status = CALL_OK;
  if (group->sealed) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (group->launches == SIZE_MAX || !object_retain(&group->object)) {
    status = CALL_LIMIT;
  } else {
    ++group->launches;
  }
  unlock_group(group);
  return status;
}

void execution_group_launch_end(struct execution_group *group)
{
  if (!group) {
    return;
  }
  lock_group(group);
  KASSERT(group->launches);
  --group->launches;
  bool completed = complete_locked(group);
  unlock_group(group);
  if (completed) {
    readiness_notify();
  }
  object_release(&group->object);
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
    for (size_t i = 0; i < count; ++i) {
      struct execution_group_member *member = task_group_member(tasks[i]);
      member->next = group->first_member;
      group->first_member = member;
    }
    group->members += count;
    user_task_publish_group(tasks, count);
  }
  unlock_group(group);
  return status;
}

void execution_group_member_detach(struct execution_group *group,
    struct execution_group_member *member)
{
  KASSERT(arch_cpu_index() == 0);
  lock_group(group);
  struct execution_group_member **link = &group->first_member;
  while (*link != member) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = member->next;
  member->next = NULL;
  unlock_group(group);
}

void execution_group_member_complete(struct execution_group *group)
{
  KASSERT(arch_cpu_index() == 0);
  lock_group(group);
  KASSERT(group->members);
  --group->members;
  bool completed = complete_locked(group);
  unlock_group(group);
  if (completed) {
    readiness_notify();
  }
  object_release(&group->object);
}

void execution_group_cleanup_begin(struct execution_group *group)
{
  lock_group(group);
  KASSERT(!group->complete && group->cleanup_pending != SIZE_MAX);
  KASSERT(object_retain(&group->object));
  ++group->cleanup_pending;
  unlock_group(group);
}

void execution_group_cleanup_end(struct execution_group *group)
{
  lock_group(group);
  KASSERT(group->cleanup_pending);
  --group->cleanup_pending;
  bool completed = complete_locked(group);
  unlock_group(group);
  if (completed) {
    readiness_notify();
  }
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
  bool last = --group->controllers == 0;
  if (last) {
    stop_locked(group);
  }
  unlock_group(group);
  if (last) {
    clipboard_stop_notify();
    readiness_notify();
  }
}

static struct syscall_result wait_group(struct execution_group *group)
{
  struct task_wait_link *waiter = task_wait_link_prepare();
  lock_group(group);
  if (!group->complete && !task_stop_requested()) {
    waiter->next = group->waiters;
    group->waiters = waiter;
    struct task_wait *wait = waiter->wait;
    unlock_group(group);
    task_wait_sleep_interruptible(wait);
    lock_group(group);
    if (waiter->wait) {
      struct task_wait_link **link = &group->waiters;
      while (*link != waiter) {
        KASSERT(*link);
        link = &(*link)->next;
      }
      *link = waiter->next;
      waiter->next = NULL;
      waiter->wait = NULL;
    }
  }
  enum call_status status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : CALL_OK;
  KASSERT(status != CALL_OK || group->complete);
  unlock_group(group);
  return (struct syscall_result){status, 0};
}

uint64_t execution_group_ready(struct execution_group *group)
{
  uint64_t flags = cpu_save_interrupts();
  lock_group(group);
  bool complete = group->complete;
  unlock_group(group);
  cpu_restore_interrupts(flags);
  return complete ? WAIT_COMPLETE : 0;
}

struct syscall_result execution_group_call(struct execution_group *group,
    uint64_t rights, uint64_t operation, size_t request_size)
{
  if (operation != EXECUTION_GROUP_SEAL && operation != EXECUTION_GROUP_TERMINATE &&
      operation != EXECUTION_GROUP_WAIT) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  uint64_t required = operation == EXECUTION_GROUP_WAIT ?
      EXECUTION_GROUP_RIGHT_WAIT : EXECUTION_GROUP_RIGHT_CONTROL;
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (operation == EXECUTION_GROUP_WAIT) {
    return wait_group(group);
  }
  lock_group(group);
  if (operation == EXECUTION_GROUP_TERMINATE) {
    stop_locked(group);
  } else {
    group->sealed = true;
    complete_locked(group);
  }
  unlock_group(group);
  clipboard_stop_notify();
  readiness_notify();
  return (struct syscall_result){CALL_OK, 0};
}
