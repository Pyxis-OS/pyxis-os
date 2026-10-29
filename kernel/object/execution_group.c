#include <kernel/object/clipboard.h>
#include <abi/wait.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/capability.h>
#include <kernel/space.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/process.h>
#include <kernel/user.h>
#include <kernel/user/wait.h>
#include <kernel/user_memory.h>
#include <kernel/wait.h>

/* Tunable admission policy, not public ABI limits. Root depth is one. */
#define EXECUTION_GROUP_DEPTH_LIMIT 16
#define EXECUTION_GROUP_COUNT_LIMIT 256

/* IF=0; space tree -> process lifetime/table -> scheduler queues. Never
 * allocate, copy user memory or switch context while held. Final grant release may run on any CPU. */
static void lock_tree(struct space *space)
{
  while (atomic_exchange_explicit(&space->execution_groups_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_tree(struct space *space)
{
  atomic_store_explicit(&space->execution_groups_locked, false, memory_order_release);
}

static bool complete_locked(struct execution_group *group)
{
  bool completed = false;
  while (group && !group->complete && group->sealed && !group->members &&
      !group->launches && !group->cleanup_pending && !group->children) {
    group->complete = true;
    completed = true;
    while (group->waiters) {
      struct task_wait_link *waiter = group->waiters;
      group->waiters = waiter->next;
      waiter->next = NULL;
      struct task_wait *wait = waiter->wait;
      waiter->wait = NULL;
      task_wait_wake(wait);
    }
    struct execution_group *parent = group->parent;
    if (parent) {
      struct execution_group **link = &parent->first_child;
      while (*link != group) {
        KASSERT(*link);
        link = &(*link)->next_sibling;
      }
      *link = group->next_sibling;
      group->next_sibling = NULL;
      KASSERT(parent->children);
      --parent->children;
      /* Only group storage is released here: its retirement does not acquire
       * a cleanup token or execute destruction under this guard. */
      object_release(&group->object);
    }
    group = parent;
  }
  return completed;
}

void execution_group_note(struct execution_group *group, uint64_t flags)
{
  for (; group; group = group->parent) {
    atomic_fetch_or_explicit(&group->result_flags, flags, memory_order_relaxed);
  }
}

static void seal_locked(struct execution_group *group, bool stop)
{
  group->sealed = true;
  if (stop && !group->stopping) {
    if (!group->complete) {
      execution_group_note(group, EXECUTION_GROUP_ABNORMAL);
    }
    group->stopping = true;
    for (struct execution_group_member *member = group->first_member;
         member; member = member->next) {
      process_request_stop(member->process);
    }
  }
  /* Completion can unlink the child, so capture the next live edge first.
   * Depth and total allocated storage are bounded by creation admission. */
  struct execution_group *child = group->first_child;
  while (child) {
    struct execution_group *next = child->next_sibling;
    seal_locked(child, stop);
    child = next;
  }
  complete_locked(group);
}

static bool admission_closed_locked(struct execution_group *group)
{
  for (; group; group = group->parent) {
    if (group->sealed) {
      return true;
    }
  }
  return false;
}

static void destroy_group(struct kernel_object *object)
{
  struct execution_group *group = (struct execution_group *)object;
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!group->controllers && !group->members && !group->launches &&
      !group->cleanup_pending && !group->first_member && !group->waiters &&
      !group->children && !group->first_child && !group->next_sibling);
  struct execution_group *parent = group->parent;
  lock_tree(group->space);
  if (!parent) {
    struct execution_group **link = &group->space->root_groups;
    while (*link && *link != group) {
      link = &(*link)->root_next;
    }
    /* A group that failed creation was never linked. */
    if (*link) {
      *link = group->root_next;
    }
  }
  KASSERT(group->space->execution_group_count);
  --group->space->execution_group_count;
  unlock_tree(group->space);
  kfree(group);
  if (parent) {
    object_release(&parent->object);
  }
}

enum call_status execution_group_create(struct space *space,
    struct execution_group *parent, struct execution_group **result)
{
  KASSERT(arch_cpu_index() == 0 && space && (!parent || parent->space == space));
  *result = NULL;
  lock_tree(space);
  enum call_status status = CALL_OK;
  if (admission_closed_locked(parent)) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (space->execution_group_count >= EXECUTION_GROUP_COUNT_LIMIT ||
      (parent && parent->depth >= EXECUTION_GROUP_DEPTH_LIMIT)) {
    status = CALL_LIMIT;
  } else {
    ++space->execution_group_count;
  }
  unlock_tree(space);
  if (status != CALL_OK) {
    return status;
  }
  struct execution_group *group = kmalloc(sizeof(*group));
  if (!group) {
    lock_tree(space);
    --space->execution_group_count;
    unlock_tree(space);
    return CALL_NO_MEMORY;
  }
  *group = (struct execution_group){.space = space,
    .depth = parent ? parent->depth + 1 : 1, .list_key = process_list_key_next()};
  atomic_init(&group->result_flags, 0);
  object_init(&group->object, OBJECT_EXECUTION_GROUP, destroy_group);
  lock_tree(group->space);
  if (admission_closed_locked(parent)) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (parent && !object_retain(&parent->object)) {
    status = CALL_LIMIT;
  } else if (parent) {
    group->parent = parent;
    KASSERT(object_retain(&group->object));
    group->next_sibling = parent->first_child;
    parent->first_child = group;
    ++parent->children;
  } else {
    group->root_next = space->root_groups;
    space->root_groups = group;
  }
  unlock_tree(group->space);
  if (status != CALL_OK) {
    object_release(&group->object);
  } else {
    *result = group;
  }
  return status;
}

enum call_status execution_group_install(struct execution_group *group,
    struct capability_reservation *reservation,
    const struct capability_reserved_slot *slots, struct capability_grant *grants,
    handle_t *handles)
{
  KASSERT(arch_cpu_index() == 0);
  lock_tree(group->space);
  enum call_status status = admission_closed_locked(group) ? CALL_ENDPOINT_CLOSED : CALL_OK;
  if (status == CALL_OK) {
    capability_install_reserved(reservation, slots, grants, 2, handles);
  }
  unlock_tree(group->space);
  return status;
}

void execution_group_abandon(struct execution_group *group)
{
  lock_tree(group->space);
  seal_locked(group, true);
  unlock_tree(group->space);
  readiness_notify();
}

bool execution_group_contains(const struct execution_group *scope,
    const struct execution_group *target)
{
  if (!scope) {
    return true;
  }
  for (; target; target = target->parent) {
    if (scope == target) {
      return true;
    }
  }
  return false;
}

enum call_status execution_group_check(struct execution_group *group, struct space *space)
{
  if (!group) {
    return CALL_OK;
  }
  if (group->space != space) {
    return CALL_DENIED;
  }
  lock_tree(group->space);
  bool sealed = admission_closed_locked(group);
  unlock_tree(group->space);
  return sealed ? CALL_ENDPOINT_CLOSED : CALL_OK;
}

bool execution_group_thread_begin(struct execution_group *group)
{
  if (!group) {
    return true;
  }
  lock_tree(group->space);
  if (group->stopping) {
    unlock_tree(group->space);
    return false;
  }
  return true;
}

void execution_group_thread_end(struct execution_group *group)
{
  if (group) {
    unlock_tree(group->space);
  }
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
  lock_tree(group->space);
  enum call_status status = CALL_OK;
  if (admission_closed_locked(group)) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (group->launches == SIZE_MAX || !object_retain(&group->object)) {
    status = CALL_LIMIT;
  } else {
    ++group->launches;
  }
  unlock_tree(group->space);
  return status;
}

void execution_group_launch_end(struct execution_group *group)
{
  if (!group) {
    return;
  }
  lock_tree(group->space);
  KASSERT(group->launches);
  --group->launches;
  bool completed = complete_locked(group);
  unlock_tree(group->space);
  if (completed) {
    readiness_notify();
  }
  object_release(&group->object);
}

/* Tree guard held, before the tasks become runnable. */
static void list_locked(struct space *space, struct task **tasks, size_t count,
    uint64_t group_key)
{
  uint64_t now = arch_monotonic_ns();
  for (size_t i = 0; i < count; ++i) {
    struct process *process = user_task_process(tasks[i]);
    KASSERT(process->space == space && !process->listed);
    process->group_key = group_key;
    process->start_ns = now;
    process->listed = true;
    process->space_next = space->processes;
    space->processes = process;
  }
}

enum call_status execution_group_publish(struct execution_group *group,
    struct task **tasks, size_t count)
{
  KASSERT(!cpu_interrupts_enabled());
  if (!group) {
    struct space *space = user_task_process(tasks[0])->space;
    lock_tree(space);
    list_locked(space, tasks, count, 0);
    user_task_publish_group(tasks, count);
    unlock_tree(space);
    return CALL_OK;
  }
  lock_tree(group->space);
  enum call_status status = CALL_OK;
  if (admission_closed_locked(group)) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (count > SIZE_MAX - group->members) {
    status = CALL_LIMIT;
  } else {
    for (size_t i = 0; i < count; ++i) {
      struct process *process = user_task_process(tasks[i]);
      KASSERT(process->execution_group == group);
      struct execution_group_member *member = &process->group_member;
      member->next = group->first_member;
      group->first_member = member;
    }
    group->members += count;
    list_locked(group->space, tasks, count, group->list_key);
    user_task_publish_group(tasks, count);
  }
  unlock_tree(group->space);
  return status;
}

void execution_group_unlist(struct process *process)
{
  KASSERT(!cpu_interrupts_enabled());
  if (!process->listed) {
    return;
  }
  struct space *space = process->space;
  lock_tree(space);
  struct process **link = &space->processes;
  while (*link != process) {
    KASSERT(*link);
    link = &(*link)->space_next;
  }
  *link = process->space_next;
  process->space_next = NULL;
  process->listed = false;
  unlock_tree(space);
}

_Static_assert(sizeof(((struct space *)0)->name) == PROCESS_LIST_SPACE_MAX,
    "listed space names carry the whole fixed name");

static void list_group(const struct execution_group *group,
    struct process_list_record *record)
{
  *record = (struct process_list_record){
    .kind = PROCESS_LIST_GROUP,
    .state = (group->sealed ? PROCESS_LIST_GROUP_SEALED : 0) |
        (group->stopping ? PROCESS_LIST_GROUP_STOPPING : 0) |
        (group->complete ? PROCESS_LIST_GROUP_COMPLETE : 0),
    .key = group->list_key,
    .group_key = group->parent ? group->parent->list_key : 0,
    .threads = group->members > UINT32_MAX ? UINT32_MAX : (uint32_t)group->members,
    .depth = (uint32_t)group->depth,
  };
}

size_t execution_group_list(struct space *space, struct process_list_record *records,
    size_t capacity, uint64_t *total)
{
  KASSERT(!cpu_interrupts_enabled());
  size_t filled = 0;
  uint64_t seen = 0;
  lock_tree(space);
  /* Preorder over each root's subtree, without recursion. */
  for (struct execution_group *root = space->root_groups; root; root = root->root_next) {
    struct execution_group *group = root;
    for (;;) {
      if (filled < capacity) {
        list_group(group, &records[filled++]);
      }
      ++seen;
      if (group->first_child) {
        group = group->first_child;
        continue;
      }
      while (group != root && !group->next_sibling) {
        group = group->parent;
      }
      if (group == root) {
        break;
      }
      group = group->next_sibling;
    }
  }
  for (struct process *process = space->processes; process; process = process->space_next) {
    if (filled < capacity) {
      records[filled] = (struct process_list_record){0};
      process_list_sample(process, &records[filled++]);
    }
    ++seen;
  }
  unlock_tree(space);
  for (size_t i = 0; i < filled; ++i) {
    memcpy(records[i].space, space->name, sizeof(space->name));
  }
  *total = seen;
  return filled;
}

void execution_group_member_detach(struct execution_group *group,
    struct execution_group_member *member)
{
  KASSERT(!cpu_interrupts_enabled());
  struct process_result result;
  KASSERT(process_result_get(member->process, &result));
  if (result.kind != PROCESS_EXITED) {
    execution_group_note(group, EXECUTION_GROUP_ABNORMAL);
  }
  lock_tree(group->space);
  struct execution_group_member **link = &group->first_member;
  while (*link != member) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = member->next;
  member->next = NULL;
  unlock_tree(group->space);
}

void execution_group_member_complete(struct execution_group *group)
{
  KASSERT(!cpu_interrupts_enabled());
  lock_tree(group->space);
  KASSERT(group->members);
  --group->members;
  bool completed = complete_locked(group);
  unlock_tree(group->space);
  if (completed) {
    readiness_notify();
  }
  object_release(&group->object);
}

void execution_group_cleanup_begin(struct execution_group *group)
{
  lock_tree(group->space);
  KASSERT(!group->complete && group->cleanup_pending != SIZE_MAX);
  KASSERT(object_retain(&group->object));
  ++group->cleanup_pending;
  unlock_tree(group->space);
}

void execution_group_cleanup_end(struct execution_group *group)
{
  lock_tree(group->space);
  KASSERT(group->cleanup_pending);
  --group->cleanup_pending;
  bool completed = complete_locked(group);
  unlock_tree(group->space);
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
  lock_tree(group->space);
  bool retained = group->controllers != SIZE_MAX;
  if (retained) {
    ++group->controllers;
  }
  unlock_tree(group->space);
  return retained;
}

void execution_group_authority_release(struct kernel_object *object, uint64_t rights)
{
  if (object->type != OBJECT_EXECUTION_GROUP || !(rights & EXECUTION_GROUP_RIGHT_CONTROL)) {
    return;
  }
  struct execution_group *group = (struct execution_group *)object;
  lock_tree(group->space);
  KASSERT(group->controllers);
  bool last = --group->controllers == 0;
  if (last) {
    seal_locked(group, true);
  }
  unlock_tree(group->space);
  if (last) {
    clipboard_stop_notify();
    readiness_notify();
  }
}

static struct syscall_result wait_group(struct execution_group *group,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct execution_group_result reply;
  if (reply_capacity && reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_capacity && !user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct task_wait_link *waiter = task_wait_link_prepare();
  lock_tree(group->space);
  if (!group->complete && !(task_stop_requested() || task_operation_denied())) {
    waiter->next = group->waiters;
    group->waiters = waiter;
    struct task_wait *wait = waiter->wait;
    unlock_tree(group->space);
    task_wait_sleep_interruptible(wait);
    lock_tree(group->space);
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
  enum call_status status = (task_stop_requested() || task_operation_denied()) ? CALL_ENDPOINT_CLOSED : CALL_OK;
  KASSERT(status != CALL_OK || group->complete);
  reply.flags = atomic_load_explicit(&group->result_flags, memory_order_relaxed);
  unlock_tree(group->space);
  if (status != CALL_OK || !reply_capacity) {
    return (struct syscall_result){status, 0};
  }
  /* No mapping lease crosses the wait; completion is repeatable on copy failure. */
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

uint64_t execution_group_ready(struct execution_group *group)
{
  uint64_t flags = cpu_save_interrupts();
  lock_tree(group->space);
  bool complete = group->complete;
  unlock_tree(group->space);
  cpu_restore_interrupts(flags);
  return complete ? WAIT_COMPLETE : 0;
}

struct syscall_result execution_group_call(struct execution_group *group,
    uint64_t rights, uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
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
    return wait_group(group, reply_address, reply_capacity);
  }
  if (!task_operation_start()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  lock_tree(group->space);
  if (operation == EXECUTION_GROUP_TERMINATE) {
    seal_locked(group, true);
  } else {
    seal_locked(group, false);
  }
  unlock_tree(group->space);
  clipboard_stop_notify();
  readiness_notify();
  return (struct syscall_result){CALL_OK, 0};
}
