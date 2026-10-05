#include <kernel/service/profile.h>
#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/object/object.h>
#include <kernel/object/process.h>
#include <kernel/object/execution_group.h>
#include <abi/launcher.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/user.h>
#include <kernel/task.h>
#include <kernel/service/request.h>
#include <kernel/wait.h>
#include <stdatomic.h>
#include <arch/cpu_local.h>

#define TASK_STACK_SIZE (16 * 1024)

enum task_kind {
  TASK_USER,
  TASK_KERNEL,
};

struct task_wait {
  struct task *task;
  bool notified;
  bool timed;
  bool interruptible;
  uint64_t deadline;
  struct task_wait *timeout_next;
};

struct task {
  struct task *next;
  struct task_wait_link resource_wait;
  struct bsp_request *request_storage;
  struct bsp_request *bsp_request; /* Reserved through result consumption. */
  struct bsp_request *deferred_request; /* Published only after the safe handoff. */
  struct task_profile *profile;
  enum task_kind kind;
  struct process *process; /* Owned by a user task; NULL for a kernel task. */
  uintptr_t kernel_stack;
  uintptr_t saved_stack;
  uintptr_t entry, user_stack;
  struct arch_user_state cpu;
  size_t cpu_index;
  bool exited, faulted, terminated, in_syscall;
  bool relocate; /* Leave this CPU at syscall return; set under queues_locked. */
  atomic_bool stop_requested;
  struct execution_group_member group_member;
  struct execution_group *cleanup_group;
  int exit_status;
  void (*kernel_entry)(void *);
  void *argument;
  uint64_t sleep_deadline;
  struct task_wait wait_record;
  struct task_wait *wait;
  bool parked; /* queues_locked: stack saved and no CPU is executing this task. */
};

struct scheduler {
  struct task *ready_head, *ready_tail;
  struct task *current_task;
  uintptr_t stack;
  struct execution_group *cleanup_group;
  /* queues_locked. Load is queued tasks plus one while a task occupies the CPU. */
  size_t queued;
  bool running;
};

static struct scheduler *schedulers;
static struct task *completed_head;
static atomic_bool started;
static atomic_bool queues_locked;
static struct task_wait *timed_waits; /* queues_locked, expired by the BSP. */

/* Sleeping kernel tasks belong to the BSP and are accessed only with IF=0. */
static struct task *sleeping_tasks;

/* IF=0 on every caller. Protects queue links and wait state; never allocate, log,
 * switch contexts or wait for another CPU while holding this lock. */
static void lock_queues(void)
{
  while (atomic_exchange_explicit(&queues_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_queues(void)
{
  atomic_store_explicit(&queues_locked, false, memory_order_release);
}

static struct scheduler *local_scheduler(void)
{
  return &schedulers[arch_cpu_index()];
}

struct process *process_current(void)
{
  struct task *task = local_scheduler()->current_task;
  return task && task->kind == TASK_USER ? task->process : NULL;
}

bool task_stop_requested(void)
{
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  return task && task->kind == TASK_USER &&
      atomic_load_explicit(&task->stop_requested, memory_order_acquire);
}

bool task_wait_stop_requested(const struct task_wait *wait)
{
  return atomic_load_explicit(&wait->task->stop_requested, memory_order_acquire);
}

struct execution_group_member *task_group_member(struct task *task)
{
  KASSERT(task->kind == TASK_USER);
  return &task->group_member;
}

struct execution_group *task_cleanup_group(void)
{
  if (!schedulers) {
    return NULL;
  }
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  if (!task) {
    return scheduler->cleanup_group;
  }
  if (task->cleanup_group) {
    return task->cleanup_group;
  }
  return task->kind == TASK_USER ? task->process->execution_group : NULL;
}

struct execution_group *task_cleanup_set_group(struct execution_group *group)
{
  KASSERT(schedulers || !group);
  if (!schedulers) {
    return NULL;
  }
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  struct execution_group **slot = task ? &task->cleanup_group : &scheduler->cleanup_group;
  struct execution_group *previous = *slot;
  *slot = group;
  return previous;
}

[[noreturn]] static void terminate_task(void)
{
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER);
  KASSERT(!task->bsp_request && !task->deferred_request && !task->wait);
  task->terminated = true;
  user_exit(0);
}

void task_syscall_enter(void)
{
  if (task_stop_requested()) {
    terminate_task();
  }
  local_scheduler()->current_task->in_syscall = true;
}

void task_syscall_leave(void)
{
  if (task_stop_requested()) {
    terminate_task();
  }
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  task->in_syscall = false;
  if (task->relocate) {
    /* The caller's space no longer allows this CPU. Switch out before any user
     * instruction runs; the scheduler requeues the task on an allowed CPU, and
     * the rest of the syscall return runs there from this kernel stack. */
    task->relocate = false;
    arch_user_save(&task->cpu);
    arch_context_switch(&task->saved_stack, scheduler->stack);
  }
}

bool kernel_task_is_current(void (*entry)(void *), const void *argument)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  return task && task->kind == TASK_KERNEL && task->kernel_entry == entry &&
    task->argument == argument;
}

void task_init(void)
{
  KASSERT(arch_cpu_index() == 0 && !schedulers);
  size_t count = arch_cpu_count();
  KASSERT(count && count <= SIZE_MAX / sizeof(*schedulers));
  schedulers = kmalloc(count * sizeof(*schedulers));
  if (!schedulers) {
    panic("cannot allocate CPU schedulers");
  }
  memset(schedulers, 0, count * sizeof(*schedulers));
}

static void enqueue_locked(struct scheduler *scheduler, struct task *task)
{
  task->next = NULL;
  if (scheduler->ready_tail) {
    scheduler->ready_tail->next = task;
  } else {
    scheduler->ready_head = task;
  }
  scheduler->ready_tail = task;
  ++scheduler->queued;
}

static size_t load_locked(size_t cpu_index)
{
  return schedulers[cpu_index].queued + (schedulers[cpu_index].running ? 1 : 0);
}

/* Multicore userspace stays off the BSP until private memory is serviced
 * locally (SMP task 7); a single-CPU boot runs it there. */
static bool user_cpu_eligible(const struct space *space, size_t cpu_index)
{
  return space_allows_cpu(space, cpu_index) && (cpu_index != 0 || arch_cpu_count() == 1);
}

/* Only a user task outside a syscall can move: its saved state is a user-mode
 * boundary on its own kernel stack. A blocked syscall resumes where it was. */
static bool task_movable(const struct task *task)
{
  return task->kind == TASK_USER && !task->in_syscall;
}

/* queues_locked. The least-loaded CPU SPACE may use; ties prefer PREFERRED,
 * then the lowest index. */
static size_t place_locked(const struct space *space, size_t preferred)
{
  size_t best = SIZE_MAX;
  for (size_t cpu_index = 0; cpu_index < arch_cpu_count(); ++cpu_index) {
    if (!user_cpu_eligible(space, cpu_index)) {
      continue;
    }
    if (best == SIZE_MAX || load_locked(cpu_index) < load_locked(best) ||
        (load_locked(cpu_index) == load_locked(best) && cpu_index == preferred)) {
      best = cpu_index;
    }
  }
  KASSERT(best != SIZE_MAX);
  return best;
}

/* queues_locked. An idle CPU takes the first movable task it may run from the
 * busiest other queue, provided that CPU keeps at least one task. */
static struct task *pull_locked(size_t cpu_index)
{
  struct scheduler *source = NULL;
  struct task *found = NULL, *found_previous = NULL;
  size_t found_load = 1;
  for (size_t other = 0; other < arch_cpu_count(); ++other) {
    size_t load = load_locked(other);
    if (other == cpu_index || load <= found_load) {
      continue;
    }
    struct task *previous = NULL;
    for (struct task *task = schedulers[other].ready_head; task;
         previous = task, task = task->next) {
      if (task_movable(task) && user_cpu_eligible(task->process->space, cpu_index)) {
        source = &schedulers[other];
        found = task;
        found_previous = previous;
        found_load = load;
        break;
      }
    }
  }
  if (!found) {
    return NULL;
  }
  if (found_previous) {
    found_previous->next = found->next;
  } else {
    source->ready_head = found->next;
  }
  if (source->ready_tail == found) {
    source->ready_tail = found_previous;
  }
  --source->queued;
  found->next = NULL;
  found->cpu_index = cpu_index;
  return found;
}

static void notify_remote_cpu(size_t cpu_index)
{
  if (cpu_index != arch_cpu_index() &&
      atomic_load_explicit(&started, memory_order_acquire)) {
    arch_cpu_reschedule(cpu_index);
  }
}

static void enqueue(struct scheduler *scheduler, struct task *task)
{
  size_t cpu_index = task->cpu_index;
  lock_queues();
  enqueue_locked(scheduler, task);
  unlock_queues();
  /* Publication transfers ownership; only the saved CPU index is safe here. */
  notify_remote_cpu(cpu_index);
}

struct task_wait *task_wait_prepare(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && !task->exited);
  KASSERT(task->kind == TASK_USER || arch_cpu_index() == 0);
  KASSERT(!task->wait && !task->parked);
  task->wait_record = (struct task_wait){.task = task};
  return &task->wait_record;
}

struct task_wait_link *task_wait_link_prepare(void)
{
  struct task_wait *wait = task_wait_prepare();
  KASSERT(wait->task->kind == TASK_USER);
  struct task_wait_link *record = &wait->task->resource_wait;
  KASSERT(!record->next);
  *record = (struct task_wait_link){.wait = wait};
  return record;
}

uint64_t task_deadline_after_ms(uint32_t milliseconds)
{
  uint64_t now = arch_monotonic_ns();
  uint64_t duration = (uint64_t)milliseconds * UINT64_C(1000000);
  return duration > UINT64_MAX - now ? UINT64_MAX : now + duration;
}

bool task_deadline_expired(uint64_t deadline)
{
  return arch_monotonic_ns() >= deadline;
}

static void sleep_wait(struct task_wait *wait, bool timed, uint64_t deadline, bool interruptible)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && wait == &task->wait_record && wait->task == task && !task->exited);
  KASSERT(task->kind == TASK_USER || arch_cpu_index() == 0);

  lock_queues();
  wait->interruptible = interruptible;
  if (wait->notified || (interruptible && task_stop_requested()) ||
      (timed && task_deadline_expired(deadline))) {
    unlock_queues();
    return;
  }
  KASSERT(!task->wait && !task->parked);
  task->wait = wait;
  if (timed) {
    wait->timed = true;
    wait->deadline = deadline;
    wait->timeout_next = timed_waits;
    timed_waits = wait;
  }
  unlock_queues();

  if (task->kind == TASK_USER) {
    arch_user_save(&task->cpu);
  }
  arch_context_switch(&task->saved_stack, scheduler->stack);
}

void task_wait_sleep(struct task_wait *wait)
{
  sleep_wait(wait, false, 0, false);
}

void task_wait_sleep_until(struct task_wait *wait, uint64_t deadline)
{
  sleep_wait(wait, true, deadline, false);
}

bool task_wait_sleep_interruptible(struct task_wait *wait)
{
  sleep_wait(wait, false, 0, true);
  return !task_stop_requested();
}

bool task_wait_sleep_until_interruptible(struct task_wait *wait, uint64_t deadline)
{
  sleep_wait(wait, true, deadline, true);
  return !task_stop_requested();
}

static void wake_wait_locked(struct task_wait *wait)
{
  wait->notified = true;
  struct task *task = wait->task;
  if (task->parked) {
    KASSERT(task->wait == wait && !task->exited);
    task->parked = false;
    task->wait = NULL;
    enqueue_locked(&schedulers[task->cpu_index], task);
  }
  /* A wake before the switch only records notification. Enqueueing a task
   * before its stack has been saved could run it on two contexts at once. */
}

static void remove_timeout_locked(struct task_wait *wait)
{
  if (!wait->timed) {
    return;
  }
  struct task_wait **link = &timed_waits;
  while (*link != wait) {
    KASSERT(*link);
    link = &(*link)->timeout_next;
  }
  *link = wait->timeout_next;
  wait->timeout_next = NULL;
  wait->timed = false;
}

void task_request_stop(struct task *task)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  atomic_store_explicit(&task->stop_requested, true, memory_order_release);
  if (task->wait && task->wait->interruptible) {
    struct task_wait *wait = task->wait;
    remove_timeout_locked(wait);
    wake_wait_locked(wait);
  }
  size_t cpu_index = task->cpu_index;
  unlock_queues();
  notify_remote_cpu(cpu_index);
}

void task_wait_wake(struct task_wait *wait)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  remove_timeout_locked(wait);
  bool parked = wait->task->parked;
  size_t cpu_index = wait->task->cpu_index;
  wake_wait_locked(wait);
  unlock_queues();
  /* The target can consume its wait/task immediately after queue publication.
   * An early wake needs no IPI: the running task observes notified when parking. */
  if (parked) {
    notify_remote_cpu(cpu_index);
  }
}

static void expire_timed_waits(void)
{
  lock_queues();
  if (!timed_waits) {
    unlock_queues();
    return;
  }
  uint64_t now = arch_monotonic_ns();
  struct task_wait **link = &timed_waits;
  while (*link) {
    struct task_wait *wait = *link;
    if (now < wait->deadline) {
      link = &wait->timeout_next;
      continue;
    }
    *link = wait->timeout_next;
    wait->timeout_next = NULL;
    wait->timed = false;
    wake_wait_locked(wait);
  }
  unlock_queues();
}

static struct task *current_user_task(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER && !task->exited);
  return task;
}

struct bsp_request *task_bsp_request_acquire(void)
{
  struct task *task = current_user_task();
  KASSERT(task->request_storage && !task->bsp_request);
  struct bsp_request *request = task->request_storage;
  KASSERT(request->state == BSP_REQUEST_FREE);
  task->bsp_request = request;
  return request;
}

struct bsp_request *task_bsp_request_current(void)
{
  return current_user_task()->bsp_request;
}

void task_bsp_request_release(struct bsp_request *request)
{
  struct task *task = current_user_task();
  KASSERT(task->bsp_request == request && !task->deferred_request);
  KASSERT(request->state == BSP_REQUEST_COMPLETE);
  task->bsp_request = NULL;
}

void task_bsp_request_defer(struct bsp_request *request)
{
  struct task *task = current_user_task();
  KASSERT(task->bsp_request == request && !task->deferred_request);
  KASSERT(request->state == BSP_REQUEST_DEFERRED && request->wait == &task->wait_record);
  task->deferred_request = request;
}

static void publish_deferred_request(struct task *task)
{
  struct bsp_request *request = task->deferred_request;
  lock_queues();
  KASSERT(task->kind == TASK_USER && request && request == task->bsp_request);
  KASSERT(task->wait == &task->wait_record && !task->wait->notified && !task->parked);
  task->deferred_request = NULL;
  task->parked = true;
  unlock_queues();
  /* The permanent stack and kernel root are active. Publication lends the
   * inactive process; no task/request accesses may follow this call. */
  bsp_request_publish_deferred(request);
}

struct task_profile *task_profile_current(void)
{
  struct task_profile *profile = current_user_task()->profile;
  KASSERT(profile);
  return profile;
}

/* Takes the local queue's head, or, when it is empty, pulls a movable task
 * from a busier CPU. The BSP pulls nothing while it runs no multicore userspace. */
static struct task *dequeue(struct scheduler *scheduler, size_t cpu_index)
{
  lock_queues();
  struct task *task = scheduler->ready_head;
  if (task) {
    scheduler->ready_head = task->next;
    if (!scheduler->ready_head) {
      scheduler->ready_tail = NULL;
    }
    --scheduler->queued;
    task->next = NULL;
  } else if (cpu_index != 0) {
    task = pull_locked(cpu_index);
  }
  scheduler->running = task != NULL;
  unlock_queues();
  return task;
}

/* A task preempted at a user-mode boundary moves when another CPU it may use
 * is at least two tasks lighter, so equal neighbours do not trade it each tick.
 * The destination's reschedule IPI is its only notification. */
static void requeue_preempted(struct task *task, size_t cpu_index)
{
  size_t destination = cpu_index;
  lock_queues();
  if (task_movable(task)) {
    struct space *space = task->process->space;
    size_t target = place_locked(space, cpu_index);
    if (!user_cpu_eligible(space, cpu_index) ||
        load_locked(target) + 2 <= load_locked(cpu_index)) {
      destination = target;
    }
  }
  task->cpu_index = destination;
  enqueue_locked(&schedulers[destination], task);
  unlock_queues();
  notify_remote_cpu(destination);
}

[[noreturn]] static void enter_task(void)
{
  struct task *task = local_scheduler()->current_task;
  if (task->kind == TASK_USER) {
    arch_enter_user(task->entry, task->user_stack, task->process->startup_address);
  }

  cpu_enable_interrupts();
  task->kernel_entry(task->argument);
  cpu_disable_interrupts();
  task->exited = true;
  arch_context_switch(&task->saved_stack, local_scheduler()->stack);
  panic("resumed an exited kernel task");
}

static enum mm_result allocate_task(struct task **result)
{
  struct task *task = kmalloc(sizeof(*task));
  if (!task) {
    return MM_NO_MEMORY;
  }
  memset(task, 0, sizeof(*task));
  atomic_init(&task->stop_requested, false);
  task->group_member.task = task;

  enum mm_result status = vm_alloc(vm_kernel_space(), TASK_STACK_SIZE,
                                    PAGE_SIZE, PAGE_WRITE, &task->kernel_stack);
  if (status != MM_OK) {
    kfree(task);
    return status;
  }

  task->saved_stack = arch_context_prepare(task->kernel_stack + TASK_STACK_SIZE,
                                           enter_task);
  *result = task;
  return MM_OK;
}

static void free_task(struct task *task)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!task->bsp_request && !task->deferred_request);
  KASSERT(!task->wait && !task->wait_record.timed && !task->resource_wait.next);
  bsp_request_storage_destroy(task->request_storage);
  profile_storage_destroy(task->profile);
  KASSERT(vm_free(vm_kernel_space(), task->kernel_stack, TASK_STACK_SIZE) == MM_OK);
  kfree(task);
}

enum mm_result kernel_task_create(void (*entry)(void *), void *argument)
{
  KASSERT(arch_cpu_index() == 0);
  if (!schedulers || !entry) {
    return MM_INVALID;
  }

  struct task *task;
  enum mm_result result = allocate_task(&task);
  if (result != MM_OK) {
    return result;
  }

  task->kind = TASK_KERNEL;
  task->kernel_entry = entry;
  task->argument = argument;
  enqueue(&schedulers[0], task);
  return MM_OK;
}

void task_space_close_setup(struct space *space)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  space->setup_open = false;
  unlock_queues();
}

enum call_status task_space_set_affinity(struct space *space, const uint64_t *cpus)
{
  struct task *task = current_user_task();
  KASSERT(task->process->space == space);
  lock_queues();
  if (!space->setup_open) {
    unlock_queues();
    return CALL_ENDPOINT_CLOSED;
  }
  memcpy(space->effective_cpus, cpus, space_cpu_words() * sizeof(*cpus));
  /* Setup is open only before the first launch, so the caller is the space's
   * only task and nothing else needs re-placing. */
  task->relocate = !user_cpu_eligible(space, task->cpu_index);
  unlock_queues();
  return CALL_OK;
}

enum mm_result user_task_prepare(struct process *process, uintptr_t entry,
                                 uintptr_t stack_top, size_t preferred_cpu,
                                 struct task **result)
{
  KASSERT(arch_cpu_index() == 0);
  *result = NULL;
  if (!schedulers || !process || !process->startup_address ||
      !arch_user_entry_valid(entry, stack_top)) {
    return MM_INVALID;
  }
  bool placeable = false;
  for (size_t cpu_index = 0; cpu_index < arch_cpu_count(); ++cpu_index) {
    placeable |= user_cpu_eligible(process->space, cpu_index);
  }
  if (!placeable) {
    return MM_INVALID;
  }

  struct page_translation code, stack;
  if (vm_query(process->address_space, entry, &code) != MM_OK ||
      vm_query(process->address_space, stack_top - 1, &stack) != MM_OK ||
      (code.permissions & (PAGE_USER | PAGE_EXEC)) != (PAGE_USER | PAGE_EXEC) ||
      (stack.permissions & (PAGE_USER | PAGE_WRITE)) != (PAGE_USER | PAGE_WRITE)) {
    return MM_INVALID;
  }

  struct task *task;
  enum mm_result status = allocate_task(&task);
  if (status != MM_OK) {
    return status;
  }

  task->request_storage = bsp_request_storage_create();
  if (!task->request_storage) {
    free_task(task);
    return MM_NO_MEMORY;
  }
  task->profile = profile_storage_create();
  if (!task->profile) {
    free_task(task);
    return MM_NO_MEMORY;
  }

  task->kind = TASK_USER;
  task->process = process;
  task->entry = entry;
  task->user_stack = stack_top;
  /* Holds only the placement preference until publication places the task. */
  task->cpu_index = preferred_cpu;
  arch_user_state_init(&task->cpu);
  *result = task;
  return MM_OK;
}

void user_task_discard_prepared(struct task *task)
{
  KASSERT(arch_cpu_index() == 0 && task && task->kind == TASK_USER);
  free_task(task);
}

void user_task_publish_group(struct task **tasks, size_t count)
{
  KASSERT(arch_cpu_index() == 0 && count && count <= LAUNCH_BATCH_MAX);
  /* Published tasks may run and retire as soon as the lock drops, so keep
   * only their destination indices for notification. */
  size_t destinations[LAUNCH_BATCH_MAX];
  size_t destination_count = 0;
  lock_queues();
  for (size_t i = 0; i < count; ++i) {
    KASSERT(tasks[i] && tasks[i]->kind == TASK_USER);
    /* Each placement counts the members already queued, so a batch spreads. */
    size_t cpu_index = place_locked(tasks[i]->process->space, tasks[i]->cpu_index);
    tasks[i]->cpu_index = cpu_index;
    enqueue_locked(&schedulers[cpu_index], tasks[i]);
    bool seen = false;
    for (size_t j = 0; j < destination_count; ++j) {
      seen |= destinations[j] == cpu_index;
    }
    if (!seen) {
      destinations[destination_count++] = cpu_index;
    }
  }
  unlock_queues();
  for (size_t i = 0; i < destination_count; ++i) {
    notify_remote_cpu(destinations[i]);
  }
}

enum mm_result user_task_create(struct process *process, uintptr_t entry,
                                uintptr_t stack_top)
{
  struct task *task;
  enum mm_result status = user_task_prepare(process, entry, stack_top, SIZE_MAX, &task);
  if (status == MM_OK) {
    /* Publication transfers process and stack ownership. */
    user_task_publish_group(&task, 1);
  }
  return status;
}

static void complete_task(struct task *task)
{
  KASSERT(!task->bsp_request && !task->deferred_request);
  lock_queues();
  task->next = completed_head;
  completed_head = task;
  unlock_queues();
  /* The BSP can free task immediately after the unlock. */
}

static void reap_completed(void)
{
  lock_queues();
  struct task *task = completed_head;
  completed_head = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->next;
    struct process_control *control = NULL;
    struct execution_group *execution_group = NULL;
    struct process_result result = {0};
    if (task->kind == TASK_USER) {
      /* Transfer the execution owner's reference before freeing the process,
       * and end the observer's task link before this task can be freed. */
      control = task->process->control;
      task->process->control = NULL;
      if (control) {
        process_control_detach_task(control);
      }
      execution_group = task->process->execution_group;
      task->process->execution_group = NULL;
      if (execution_group) {
        execution_group_member_detach(execution_group, &task->group_member);
      }
      result.kind = task->faulted ? PROCESS_FAULTED :
          task->terminated ? PROCESS_TERMINATED : PROCESS_EXITED;
      result.exit_status = result.kind == PROCESS_EXITED ? task->exit_status : 0;
      struct execution_group *previous = object_cleanup_enter(execution_group);
      KASSERT(process_destroy(task->process) == MM_OK);
      object_cleanup_leave(previous);
    }
    if (task->kind == TASK_USER) {
      if (task->terminated) {
        klog("userspace: CPU %zu terminated task released\n", task->cpu_index);
      } else if (task->faulted) {
        klog("userspace: CPU %zu faulted task released\n", task->cpu_index);
      } else {
        ktrace("userspace: CPU %zu exited with status %d; address space released\n",
             task->cpu_index, task->exit_status);
      }
    }
    free_task(task);
    if (control) {
      process_control_complete(control, result);
      struct execution_group *previous = object_cleanup_enter(execution_group);
      object_release(&control->object);
      object_cleanup_leave(previous);
    }
    if (execution_group) {
      execution_group_member_complete(execution_group);
    }
    task = next;
  }
}

static void wake_sleepers(void)
{
  if (!sleeping_tasks) {
    return;
  }
  uint64_t now = arch_monotonic_ns();
  struct task **link = &sleeping_tasks;

  while (*link) {
    struct task *task = *link;
    if (now < task->sleep_deadline) {
      link = &task->next;
      continue;
    }

    *link = task->next;
    task->sleep_deadline = 0;
    enqueue(&schedulers[0], task);
  }
}

void kernel_task_sleep_until(uint64_t deadline)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && (flags & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_KERNEL && !task->exited);

  task->sleep_deadline = task_deadline_expired(deadline) ? 0 : deadline;
  arch_context_switch(&task->saved_stack, scheduler->stack);
  cpu_restore_interrupts(flags);
}

void kernel_task_yield_if_runnable(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && (flags & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_KERNEL && !task->exited);

  lock_queues();
  bool runnable = scheduler->ready_head != NULL;
  unlock_queues();
  if (runnable) {
    arch_context_switch(&task->saved_stack, scheduler->stack);
  }
  cpu_restore_interrupts(flags);
}

[[noreturn]] void task_schedule(void)
{
  size_t cpu_index = arch_cpu_index();
  if (cpu_index == 0) {
    KASSERT(schedulers && !atomic_load_explicit(&started, memory_order_relaxed));
    /* Publish scheduler storage and initialized console state to every AP. */
    atomic_store_explicit(&started, true, memory_order_release);
  } else {
    while (!atomic_load_explicit(&started, memory_order_acquire)) {
      cpu_wait_interrupt();
    }
  }

  struct scheduler *scheduler = local_scheduler();
  bool idle_reported = false;
  for (;;) {
    if (cpu_index == 0) {
      expire_timed_waits();
      reap_completed();
      object_reap();
      wake_sleepers();
    }

    struct task *task = dequeue(scheduler, cpu_index);
    scheduler->current_task = task;
    if (!task) {
      if (!idle_reported) {
        ktrace("scheduler: CPU %zu no runnable tasks, idle\n", cpu_index);
        idle_reported = true;
      }
      /* Remote ready-queue publication is followed by an IPI. With IF=0 here,
       * STI/HLT also handles a notification arriving after the empty check.
       * The timer still services deadlines and BSP-only request/cleanup queues. */
      cpu_wait_interrupt();
      continue;
    }
    if (task->kind == TASK_USER) {
      idle_reported = false;
    }

    if (task->kind == TASK_USER && !task->in_syscall && task_stop_requested()) {
      task->terminated = task->exited = true;
      scheduler->current_task = NULL;
      complete_task(task);
      continue;
    }

    /* Reload CR3 before touching a newly published task stack. This also
     * discards translations from a previous use of its kernel virtual range. */
    struct vm_space *address_space = task->kind == TASK_USER ?
                                    task->process->address_space : vm_kernel_space();
    KASSERT(vm_space_activate(address_space) == MM_OK);
    if (task->kind == TASK_USER) {
      arch_user_set_kernel_stack(task->kernel_stack + TASK_STACK_SIZE);
      arch_user_restore(&task->cpu);
    }
    arch_context_switch(&scheduler->stack, task->saved_stack);

    /* We are back on this CPU's permanent stack. Drop the private root and
     * flush translations before publishing completion to the BSP reaper. */
    KASSERT(vm_space_activate(vm_kernel_space()) == MM_OK);
    arch_user_set_kernel_stack(0);
    scheduler->current_task = NULL;
    if (task->exited) {
      complete_task(task);
    } else if (task->deferred_request) {
      publish_deferred_request(task);
    } else if (task->wait) {
      lock_queues();
      if (task->wait->notified) {
        task->wait = NULL;
        enqueue_locked(scheduler, task);
      } else {
        task->parked = true;
      }
      unlock_queues();
    } else if (task->sleep_deadline) {
      task->next = sleeping_tasks;
      sleeping_tasks = task;
    } else {
      requeue_preempted(task, cpu_index);
    }
  }
}

void task_preempt(bool user_mode)
{
  /* AP kernel execution remains non-preemptible, including startup waits.
   * It must not inspect scheduler storage before acquiring started. */
  if (!user_mode && arch_cpu_index() != 0) {
    return;
  }
  if (!schedulers) {
    return;
  }

  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  if (!task) {
    return;
  }
  KASSERT(!task->exited);
  if (task->kind == TASK_USER && !user_mode) {
    return;
  }

  if (task->kind == TASK_USER && user_mode && task_stop_requested()) {
    terminate_task();
  }
  if (arch_cpu_index() == 0) {
    expire_timed_waits();
    wake_sleepers();
  }

  lock_queues();
  bool schedule_needed = scheduler->ready_head != NULL ||
    (arch_cpu_index() == 0 &&
     completed_head != NULL);
  unlock_queues();
  if (arch_cpu_index() == 0 && object_reap_pending()) {
    schedule_needed = true;
  }
  if (!schedule_needed) {
    return;
  }

  if (task->kind == TASK_USER) {
    arch_user_save(&task->cpu);
  }
  arch_context_switch(&task->saved_stack, scheduler->stack);
}

[[noreturn]] void user_exit(int status)
{
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_USER && !task->exited);
  task->exit_status = status;
  task->exited = true;
  arch_context_switch(&task->saved_stack, scheduler->stack);
  panic("resumed an exited user task");
}

[[noreturn]] void user_fault(void)
{
  struct task *task = local_scheduler()->current_task;
  KASSERT(task);
  task->faulted = true;
  user_exit(-1);
}
