#include <kernel/service/profile.h>
#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/object/object.h>
#include <kernel/object/clock.h>
#include <kernel/object/process.h>
#include <kernel/object/execution_group.h>
#include <abi/launcher.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>
#include <kernel/space.h>
#include <kernel/user.h>
#include <kernel/task.h>
#include <kernel/service/request.h>
#include <kernel/spinlock.h>
#include <kernel/wait.h>
#include <stdatomic.h>
#include <kernel/mm/private.h>
#include <kernel/object/thread.h>
#include <kernel/object/forwarder.h>
#include <kernel/user/address_wait.h>
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
  /* On-CPU TSC cycles, split at syscall entry/leave. cpu_mark is this task's
   * last accounting boundary on its current CPU; only that CPU updates them. */
  uint64_t cpu_user_cycles, cpu_kernel_cycles, cpu_mark;
  enum task_kind kind;
  struct process *process; /* Borrowed submitted/prepared lifetime; NULL for workers. */
  uintptr_t kernel_stack;
  uintptr_t saved_stack;
  uintptr_t entry, user_stack;
  struct arch_user_state cpu;
  size_t cpu_index;
  bool exited, in_syscall;
  bool ram_reaper; /* Fixed boot-created RAM owner, with audited wait/yield paths. */
  atomic_bool vm_continuation;
  bool vm_kernel; /* Current syscall owns a kernel-root continuation, never movable. */
  bool thread_exit_pending;
  size_t vm_borrowers; /* Process VM guard; owned by this task's active accesses. */
  struct process_vm_held vm_held;
  bool vm_parked; /* queues_locked; separate from resource waits. */
  bool relocate; /* Leave this CPU at syscall return; set under queues_locked. */
  atomic_bool stop_requested;
  struct execution_group *cleanup_group;
  void (*kernel_entry)(void *);
  void *argument;
  uint64_t sleep_deadline;
  struct task_wait wait_record;
  struct task_wait *wait;
  bool parked; /* queues_locked: stack saved and no CPU is executing this task. */
  /* Cold lifecycle storage stays after the scheduler/copy state. */
  struct process_task_member process_member;
  uintptr_t user_argument;
  struct private_context stack_context, fs_context;
  struct thread_control *thread_control;
  struct thread_result thread_result;
  bool fs_bound, faulted;
  struct address_wait_record address_wait_record;
  struct address_wait_record *address_wait; /* queues_locked; published before sleep. */
  bool address_wait_expired; /* queues_locked; queued for scheduler cancellation. */
  struct forwarder_operation forwarder_storage;
  struct forwarder_operation *forwarder_operation; /* Own embedded or borrow caller. */
};

struct scheduler {
  struct task *ready_head, *ready_tail;
  struct task *current_task;
  struct task_wait *timed_waits; /* queues_locked, sorted by deadline. */
  uintptr_t stack;
  struct execution_group *cleanup_group;
  /* queues_locked. Load is queued tasks plus one while a task occupies the CPU. */
  size_t queued;
  bool running;
  struct task_wait *expired_address_waits; /* Local CPU writes; queues_locked, not notified. */
  /* Local scheduler loop only: the last TSC boundary and this CPU's totals. */
  uint64_t cpu_mark, cpu_user_cycles, cpu_kernel_cycles, cpu_idle_cycles;
};

static struct scheduler *schedulers;
static struct task *completed_head;
static struct task **ram_retired; /* queues_locked; indexed by departure CPU. */
static atomic_bool started;
static struct spinlock queues_locked;

/* Sorted kernel sleepers belong to the BSP and are accessed only with IF=0. */
static struct task *sleeping_tasks;

/* Power-off hold. Changed under queues_locked; read without it at user
 * boundaries, where a stale value only moves the stop to the next boundary.
 * Held tasks are parked user tasks with saved stacks, outside every queue. */
static atomic_bool user_hold;
static struct task *held_tasks;

/* Protects queue links and wait state. Resource and group locks may be held
 * while taking it; it nests inside them, never the reverse. */
static void lock_queues(void)
{
  spin_lock(&queues_locked);
}

static void unlock_queues(void)
{
  spin_unlock(&queues_locked);
}

static struct scheduler *local_scheduler(void)
{
  return &schedulers[arch_cpu_index()];
}

/* At most one clock reading per timer pass, taken only when expiry or a rearm
 * needs it. Later steps reuse it, so it can be slightly old. */
struct timer_clock {
  bool taken;
  uint64_t now;
};

static uint64_t timer_clock_now(struct timer_clock *clock)
{
  if (!clock->taken) {
    clock->now = arch_monotonic_ns();
    clock->taken = true;
  }
  return clock->now;
}

static void arm_local_deadline(struct timer_clock *clock)
{
  lock_queues();
  struct task_wait *wait = local_scheduler()->timed_waits;
  uint64_t deadline = wait ? wait->deadline : UINT64_MAX;
  unlock_queues();
  if (arch_cpu_index() == 0 && sleeping_tasks &&
      sleeping_tasks->sleep_deadline < deadline) {
    deadline = sleeping_tasks->sleep_deadline;
  }
  /* Only this CPU inserts deadlines. A remote removal can leave one harmless
   * earlier interrupt armed, but cannot introduce a deadline we missed. */
  if (arch_timer_arm_needed(deadline)) {
    arch_timer_arm(deadline, timer_clock_now(clock));
  }
}

struct process *process_current(void)
{
  struct task *task = local_scheduler()->current_task;
  return task && task->kind == TASK_USER ? task->process : NULL;
}

struct task *task_current_actor(void)
{
  return schedulers ? local_scheduler()->current_task : NULL;
}

struct task *task_current_user(void)
{
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  return task && task->kind == TASK_USER ? task : NULL;
}

bool task_vm_continuation(struct task *task)
{
  return atomic_load_explicit(&task->vm_continuation, memory_order_acquire);
}

void task_vm_set_continuation(struct task *task, bool admitted)
{
  atomic_store_explicit(&task->vm_continuation, admitted, memory_order_release);
}

size_t task_vm_borrow_count(struct task *task)
{
  return task->vm_borrowers;
}

void task_vm_borrow_enter(struct task *task)
{
  KASSERT(task->vm_borrowers != SIZE_MAX);
  ++task->vm_borrowers;
}

void task_vm_borrow_leave(struct task *task)
{
  KASSERT(task->vm_borrowers);
  --task->vm_borrowers;
}

bool task_stop_requested(void)
{
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  return task && task->kind == TASK_USER &&
      atomic_load_explicit(&task->stop_requested, memory_order_acquire);
}

struct forwarder_operation *task_forwarder_operation(void)
{
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  return task ? task->forwarder_operation : NULL;
}

struct forwarder_operation *task_forwarder_ensure(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER);
  if (!task->forwarder_operation) {
    forwarder_operation_init(&task->forwarder_storage);
    task->forwarder_operation = &task->forwarder_storage;
  }
  return task->forwarder_operation;
}

struct forwarder_operation *task_forwarder_set(struct forwarder_operation *operation)
{
  uint64_t flags = cpu_save_interrupts();
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  KASSERT(task);
  struct forwarder_operation *previous = task->forwarder_operation;
  task->forwarder_operation = operation;
  cpu_restore_interrupts(flags);
  return previous;
}

void task_forwarder_finish(void)
{
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  if (task && task->forwarder_operation) {
    KASSERT(task->forwarder_operation == &task->forwarder_storage);
    task->forwarder_operation = NULL;
    forwarder_operation_release(&task->forwarder_storage);
  }
}

bool task_operation_start(void)
{
  struct forwarder_operation *operation = task_forwarder_operation();
  if (!operation) {
    return true;
  }
  uint64_t flags = cpu_save_interrupts();
  bool result = forwarder_operation_start(operation);
  cpu_restore_interrupts(flags);
  return result;
}

bool task_operation_denied(void)
{
  struct forwarder_operation *operation = task_forwarder_operation();
  if (!operation) {
    return false;
  }
  uint64_t flags = cpu_save_interrupts();
  bool result = forwarder_operation_denied(operation);
  cpu_restore_interrupts(flags);
  return result;
}

bool task_wait_stop_requested(const struct task_wait *wait)
{
  return atomic_load_explicit(&wait->task->stop_requested, memory_order_acquire);
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

[[noreturn]] static void finish_user_task(struct process_result result)
{
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_USER && !task->exited);
  KASSERT(!task->bsp_request && !task->deferred_request && !task->wait);
  KASSERT(!task->vm_borrowers && !task->vm_kernel);
  if (task_vm_continuation(task)) {
    process_vm_syscall_leave(task->process, task);
  }
  process_set_result(task->process, result);
  task->faulted = result.kind == PROCESS_FAULTED;
  task->thread_result = (struct thread_result){0};
  task->exited = true;
  arch_context_switch(&task->saved_stack, scheduler->stack);
  panic("resumed an exited user task");
}

[[noreturn]] static void terminate_task(void)
{
  finish_user_task((struct process_result){.kind = PROCESS_TERMINATED});
}

/* Charges the current task's time since its last boundary on this CPU to user
 * or kernel time. IF=0, on the CPU that runs or just ran TASK. */
static uint64_t account_task(struct task *task, bool kernel)
{
  struct scheduler *scheduler = local_scheduler();
  uint64_t now = arch_tsc_read();
  uint64_t cycles = now > task->cpu_mark ? now - task->cpu_mark : 0;
  task->cpu_mark = now;
  if (kernel) {
    task->cpu_kernel_cycles += cycles;
    scheduler->cpu_kernel_cycles += cycles;
  } else {
    task->cpu_user_cycles += cycles;
    scheduler->cpu_user_cycles += cycles;
  }
  return now;
}

void task_syscall_enter(void)
{
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  for (;;) {
    if (task_stop_requested()) {
      terminate_task();
    }
    if (process_vm_syscall_enter(task->process, task)) {
      break;
    }
    arch_user_save(&task->cpu);
    arch_context_switch(&task->saved_stack, scheduler->stack);
    /* Before admission this task is still movable. Do not carry CPU-local
     * scheduler stack ownership across a rejected-entry handoff. */
    scheduler = local_scheduler();
  }
  account_task(task, false);
  task->in_syscall = true;
}

void task_syscall_leave(void)
{
  if (task_stop_requested()) {
    terminate_task();
  }
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  if (task->thread_exit_pending) {
    KASSERT(!task->bsp_request && !task->deferred_request && !task->wait &&
        !task->vm_borrowers);
    process_vm_syscall_leave(task->process, task);
    task->exited = true;
    arch_context_switch(&task->saved_stack, scheduler->stack);
    panic("resumed an exited user task");
  }
  process_vm_syscall_leave(task->process, task);
  account_task(task, true);
  task->in_syscall = false;
  if (task->relocate || atomic_load_explicit(&user_hold, memory_order_acquire) ||
      process_vm_execution_closed(task->process)) {
    /* The caller's space no longer allows this CPU, or user tasks are held.
     * Switch out before any user instruction runs; the scheduler requeues or
     * holds the task, and the rest of the syscall return later runs from this
     * kernel stack. */
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

static size_t placement_load_locked(size_t cpu_index, size_t preparation_cpu)
{
  struct scheduler *scheduler = &schedulers[cpu_index];
  return load_locked(cpu_index) -
    (cpu_index == preparation_cpu && scheduler->running ? 1 : 0);
}

/* Only a user task outside a syscall can move: its saved state is a user-mode
 * boundary on its own kernel stack. A blocked syscall resumes where it was. */
static bool task_movable(const struct task *task)
{
  return task->kind == TASK_USER && !task->in_syscall;
}

static bool siblings_idle_locked(size_t cpu_index, size_t preparation_cpu)
{
  for (size_t other = 0; other < arch_cpu_count(); ++other) {
    if (other != cpu_index && arch_cpus_share_core(cpu_index, other) &&
        placement_load_locked(other, preparation_cpu)) {
      return false;
    }
  }
  return true;
}

/* queues_locked. Ties between least-loaded allowed CPUs prefer idle siblings,
 * then PREFERRED, then the lowest AP, then the BSP. */
static size_t place_locked(const struct space *space, size_t preferred)
{
  /* Initial publication discounts only this caller's temporary preparation
   * work. Actual running/queued state and idle stealing remain unchanged. */
  struct task *preparer = task_current_user();
  KASSERT(!preparer || (preparer->in_syscall && preparer->vm_kernel));
  size_t preparation_cpu = preparer ? arch_cpu_index() : SIZE_MAX;
  size_t count = arch_cpu_count();
  size_t best = SIZE_MAX;
  size_t best_load = SIZE_MAX;
  bool best_idle = false;
  for (size_t i = 0; i < count; ++i) {
    size_t cpu_index = (i + 1) % count;
    if (!space_allows_cpu(space, cpu_index)) {
      continue;
    }
    size_t load = placement_load_locked(cpu_index, preparation_cpu);
    if (load > best_load) {
      continue;
    }
    bool idle = siblings_idle_locked(cpu_index, preparation_cpu);
    if (best == SIZE_MAX || load < best_load || (idle && !best_idle) ||
        (idle == best_idle && cpu_index == preferred)) {
      best = cpu_index;
      best_load = load;
      best_idle = idle;
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
      if (task_movable(task) && space_allows_cpu(task->process->space, cpu_index)) {
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

struct process_vm_held *task_vm_hold(struct task *task)
{
  lock_queues();
  KASSERT(task->kind == TASK_USER && !task->vm_parked && !task->wait &&
      !task->parked && !task->vm_held.next && !task->vm_borrowers);
  task->vm_parked = true;
  task->vm_held.task = task;
  unlock_queues();
  return &task->vm_held;
}

void task_vm_resume(struct task *task)
{
  lock_queues();
  KASSERT(task->vm_parked && !task->vm_held.next);
  task->vm_parked = false;
  size_t cpu_index = task->cpu_index;
  enqueue_locked(&schedulers[cpu_index], task);
  unlock_queues();
  notify_remote_cpu(cpu_index);
}

struct task_wait *task_wait_prepare(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && !task->exited);
  KASSERT(task->kind == TASK_USER || task->ram_reaper || arch_cpu_index() == 0);
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

struct address_wait_record *task_address_wait_prepare(void)
{
  struct task *task = task_current_user();
  KASSERT(task && task->in_syscall && !task->address_wait &&
      !task->address_wait_expired);
  struct task_wait *wait = task_wait_prepare();
  struct address_wait_record *record = &task->address_wait_record;
  KASSERT(!record->linked);
  *record = (struct address_wait_record){.wait = wait, .process = task->process};
  return record;
}

bool task_address_wait_publish(struct address_wait_record *record)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER && task->in_syscall &&
      record == &task->address_wait_record && record->wait == &task->wait_record);
  lock_queues();
  KASSERT(!task->address_wait && !task->address_wait_expired);
  if (task_stop_requested()) {
    unlock_queues();
    return false;
  }
  task->address_wait = record;
  unlock_queues();
  return true;
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

enum wait_precision {
  WAIT_PRECISE,
  WAIT_COARSE,
};

static void sleep_wait(struct task_wait *wait, bool timed, uint64_t deadline,
    bool interruptible, enum wait_precision precision)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && wait == &task->wait_record && wait->task == task && !task->exited);
  KASSERT(!task->vm_borrowers);
  KASSERT(task->kind == TASK_USER || task->ram_reaper || arch_cpu_index() == 0);

  lock_queues();
  wait->interruptible = interruptible;
  if (wait->notified || (interruptible && task_stop_requested()) ||
      (timed && (precision == WAIT_COARSE ? clock_coarse_ns() >= deadline :
          task_deadline_expired(deadline)))) {
    unlock_queues();
    return;
  }
  KASSERT(!task->wait && !task->parked);
  task->wait = wait;
  if (timed) {
    wait->timed = true;
    wait->deadline = deadline;
    struct task_wait **link = &scheduler->timed_waits;
    while (*link && (*link)->deadline <= deadline) {
      link = &(*link)->timeout_next;
    }
    wait->timeout_next = *link;
    *link = wait;
  }
  unlock_queues();

  if (timed) {
    struct timer_clock clock = {};
    arm_local_deadline(&clock);
  }
  if (task->kind == TASK_USER) {
    arch_user_save(&task->cpu);
  }
  arch_context_switch(&task->saved_stack, scheduler->stack);
}

void task_wait_sleep(struct task_wait *wait)
{
  sleep_wait(wait, false, 0, false, WAIT_PRECISE);
}

void task_wait_sleep_until(struct task_wait *wait, uint64_t deadline)
{
  sleep_wait(wait, true, deadline, false, WAIT_PRECISE);
}

void task_wait_sleep_until_coarse(struct task_wait *wait, uint64_t deadline)
{
  KASSERT(local_scheduler()->current_task->kind == TASK_KERNEL);
  sleep_wait(wait, true, deadline, false, WAIT_COARSE);
}

bool task_wait_sleep_interruptible(struct task_wait *wait)
{
  struct forwarder_operation *operation = task_forwarder_operation();
  forwarder_operation_wait(operation, wait);
  sleep_wait(wait, false, 0, true, WAIT_PRECISE);
  forwarder_operation_unwait(operation, wait);
  return !task_stop_requested() && !task_operation_denied();
}

bool task_wait_sleep_until_interruptible(struct task_wait *wait, uint64_t deadline)
{
  struct forwarder_operation *operation = task_forwarder_operation();
  forwarder_operation_wait(operation, wait);
  sleep_wait(wait, true, deadline, true, WAIT_PRECISE);
  forwarder_operation_unwait(operation, wait);
  return !task_stop_requested() && !task_operation_denied();
}

void task_address_wait_sleep(struct address_wait_record *record, uint64_t deadline)
{
  size_t bucket = record->bucket;
  bool timed = deadline != UINT64_MAX;
  sleep_wait(record->wait, timed, deadline, false, WAIT_PRECISE);
  if (timed) {
    /* Immediate expiry can return without ever entering the timer list. A
     * committed peer result makes this cancellation an ordinary no-op. */
    address_wait_cancel(record, bucket, ADDRESS_WAIT_TIMED_OUT);
  }
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
  struct task_wait **link = &schedulers[wait->task->cpu_index].timed_waits;
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
  struct address_wait_record *address_wait = task->address_wait;
  size_t bucket = address_wait ? address_wait->bucket : 0;
  if (!address_wait && task->wait && task->wait->interruptible) {
    struct task_wait *wait = task->wait;
    remove_timeout_locked(wait);
    wake_wait_locked(wait);
  }
  size_t cpu_index = task->cpu_index;
  unlock_queues();
  if (address_wait) {
    /* Process lifetime exclusion keeps storage alive; permanent stop rejects
     * any later publication if a peer already resolved this registration. */
    address_wait_cancel(address_wait, bucket, ADDRESS_WAIT_STOPPED);
  }
  process_vm_unhold(task->process, task);
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

void task_address_wait_wake(struct address_wait_record *record)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!record->linked && record->result != ADDRESS_WAIT_PENDING);
  struct task_wait *wait = record->wait;
  lock_queues();
  KASSERT(wait->task->address_wait == record);
  wait->task->address_wait = NULL;
  remove_timeout_locked(wait);
  bool parked = wait->task->parked;
  size_t cpu_index = wait->task->cpu_index;
  wake_wait_locked(wait);
  unlock_queues();
  if (parked) {
    notify_remote_cpu(cpu_index);
  }
}

static void expire_timed_waits(struct timer_clock *clock)
{
  lock_queues();
  struct task_wait **link = &local_scheduler()->timed_waits;
  if (!*link) {
    unlock_queues();
    return;
  }
  uint64_t now = timer_clock_now(clock);
  while (*link && (*link)->deadline <= now) {
    struct task_wait *wait = *link;
    *link = wait->timeout_next;
    wait->timeout_next = NULL;
    wait->timed = false;
    if (wait->task->address_wait) {
      /* Interrupt entry cannot take a parking bucket. Keep the local task
       * asleep until the permanent scheduler stack detaches its registration. */
      KASSERT(!wait->task->address_wait_expired);
      wait->task->address_wait_expired = true;
      wait->timeout_next = local_scheduler()->expired_address_waits;
      local_scheduler()->expired_address_waits = wait;
    } else {
      wake_wait_locked(wait);
    }
  }
  unlock_queues();
}

static void cancel_expired_address_waits(struct scheduler *scheduler)
{
  /* Only this CPU edits the list; IF=0 keeps ordinary passes lock-free here. */
  if (!scheduler->expired_address_waits) {
    return;
  }
  KASSERT(!scheduler->current_task);
  for (;;) {
    lock_queues();
    struct task_wait *wait = scheduler->expired_address_waits;
    if (!wait) {
      unlock_queues();
      return;
    }
    scheduler->expired_address_waits = wait->timeout_next;
    wait->timeout_next = NULL;
    KASSERT(wait->task->address_wait_expired && wait->task->in_syscall &&
        wait->task->cpu_index == arch_cpu_index());
    wait->task->address_wait_expired = false;
    struct address_wait_record *record = &wait->task->address_wait_record;
    size_t bucket = record->bucket;
    unlock_queues();
    /* A peer may already have detached and queued the task. Its syscall cannot
     * migrate or run on this CPU before this drain finishes, so storage is live. */
    address_wait_cancel(record, bucket, ADDRESS_WAIT_TIMED_OUT);
  }
}

static struct task *current_user_task(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER && !task->exited);
  return task;
}

bool task_vm_kernel_active(void)
{
  struct task *task = task_current_user();
  return task && task->vm_kernel;
}

static void switch_vm_root(bool kernel)
{
  struct task *task = current_user_task();
  KASSERT(task->in_syscall && !task->vm_borrowers && task->vm_kernel != kernel);
  KASSERT(!task->wait && !task->deferred_request);
  arch_user_save(&task->cpu);
  task->vm_kernel = kernel;
  /* The scheduler drops the old root on its permanent stack before making
   * this continuation runnable. in_syscall pins its CPU across every yield. */
  arch_context_switch(&task->saved_stack, local_scheduler()->stack);
}

void task_vm_kernel_enter(void)
{
  switch_vm_root(true);
}

void task_vm_kernel_leave(void)
{
  switch_vm_root(false);
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
  KASSERT(request->state == BSP_REQUEST_COMPLETE || request->state == BSP_REQUEST_PREPARED);
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
 * from a busier CPU. The BSP pulls too; its kernel workers are queued locally,
 * so it pulls only while none of them is runnable. */
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
  } else {
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
    if (!space_allows_cpu(space, cpu_index) ||
        load_locked(target) + 2 <= load_locked(cpu_index)) {
      destination = target;
    }
  }
  task->cpu_index = destination;
  enqueue_locked(&schedulers[destination], task);
  unlock_queues();
  notify_remote_cpu(destination);
}

/* Scheduler, IF=0, for a user task outside a syscall that is about to run, or
 * has just stopped running, user code. True when the hold parked it. */
static bool hold_user_task(struct task *task)
{
  if (task->kind != TASK_USER || task->in_syscall) {
    return false;
  }
  lock_queues();
  bool held = atomic_load_explicit(&user_hold, memory_order_relaxed);
  if (held) {
    task->next = held_tasks;
    held_tasks = task;
  }
  unlock_queues();
  return held;
}

void task_user_hold(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  KASSERT(!atomic_load_explicit(&user_hold, memory_order_relaxed));
  atomic_store_explicit(&user_hold, true, memory_order_release);
  unlock_queues();
}

void task_user_release(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  KASSERT(atomic_load_explicit(&user_hold, memory_order_relaxed));
  atomic_store_explicit(&user_hold, false, memory_order_release);
  struct task *task = held_tasks;
  held_tasks = NULL;
  unlock_queues();
  while (task) {
    struct task *next = task->next;
    task->next = NULL;
    requeue_preempted(task, task->cpu_index);
    task = next;
  }
}

[[noreturn]] static void enter_task(void)
{
  struct task *task = local_scheduler()->current_task;
  if (task->kind == TASK_USER) {
    arch_enter_user(task->entry, task->user_stack, task->user_argument);
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
  KASSERT(vm_kernel_owner_held());
  struct task *task = kmalloc(sizeof(*task));
  if (!task) {
    return MM_NO_MEMORY;
  }
  memset(task, 0, sizeof(*task));
  atomic_init(&task->stop_requested, false);
  atomic_init(&task->vm_continuation, false);

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
  KASSERT(vm_kernel_owner_held());
  KASSERT(!task->bsp_request && !task->deferred_request);
  KASSERT(!task->wait && !task->wait_record.timed && !task->resource_wait.next);
  KASSERT(!task->address_wait && !task->address_wait_expired &&
      !task->address_wait_record.linked);
  bsp_request_storage_destroy(task->request_storage);
  profile_storage_destroy(task->profile);
  arch_user_state_destroy(&task->cpu);
  KASSERT(vm_free(vm_kernel_space(), task->kernel_stack, TASK_STACK_SIZE) == MM_OK);
  kfree(task);
}

static enum mm_result create_kernel_task(size_t cpu, bool ram_reaper,
    void (*entry)(void *), void *argument)
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
  task->cpu_index = cpu;
  task->ram_reaper = ram_reaper;
  enqueue(&schedulers[cpu], task);
  return MM_OK;
}

enum mm_result kernel_task_create(void (*entry)(void *), void *argument)
{
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    return MM_BUSY;
  }
  enum mm_result result = create_kernel_task(0, false, entry, argument);
  vm_kernel_owner_release(&owner);
  return result;
}

enum mm_result kernel_ram_reaper_create(size_t cpu, void (*entry)(void *), void *argument)
{
  KASSERT(!vm_kernel_runtime() && cpu < arch_cpu_count());
  struct vm_kernel_owner owner = {0};
  KASSERT(vm_kernel_owner_try(&owner));
  enum mm_result result = create_kernel_task(cpu, true, entry, argument);
  vm_kernel_owner_release(&owner);
  return result;
}

void task_ram_reapers_init(void)
{
  KASSERT(!vm_kernel_runtime() && !ram_retired);
  ram_retired = kmalloc(arch_cpu_count() * sizeof(*ram_retired));
  if (!ram_retired) {
    panic("cannot allocate task RAM retirement queues");
  }
  memset(ram_retired, 0, arch_cpu_count() * sizeof(*ram_retired));
}

void task_space_close_setup(struct space *space)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  space->setup_open = false;
  unlock_queues();
}

bool task_space_setup_open(struct space *space)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  bool open = space->setup_open;
  unlock_queues();
  return open;
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
  task->relocate = !space_allows_cpu(space, task->cpu_index);
  unlock_queues();
  return CALL_OK;
}

size_t task_space_usable_cpu_count(struct space *space)
{
  KASSERT(!cpu_interrupts_enabled());
  lock_queues();
  size_t count = 0;
  for (size_t cpu = 0; cpu < arch_cpu_count(); ++cpu) {
    if (space_allows_cpu(space, cpu)) {
      ++count;
    }
  }
  unlock_queues();
  return count;
}

static enum mm_result prepare_user_task(struct process *process, uintptr_t entry,
                                 uintptr_t stack_top, size_t preferred_cpu,
                                 struct task **result)
{
  process_prepare_require(process);
  *result = NULL;
  if (!schedulers || !process || !process->startup_address ||
      !process_task_available(process) ||
      !arch_user_entry_valid(entry, stack_top)) {
    return MM_INVALID;
  }
  bool placeable = false;
  for (size_t cpu_index = 0; cpu_index < arch_cpu_count(); ++cpu_index) {
    placeable |= space_allows_cpu(process->space, cpu_index);
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
  task->user_argument = process->startup_address;
  /* Holds only the placement preference until publication places the task. */
  task->cpu_index = preferred_cpu;
  if (!arch_user_state_init(&task->cpu)) {
    free_task(task);
    return MM_NO_MEMORY;
  }
  process_task_attach(process, &task->process_member, task);
  *result = task;
  return MM_OK;
}

enum mm_result user_task_prepare(struct process *process, uintptr_t entry,
    uintptr_t stack_top, size_t preferred_cpu, struct task **result)
{
  *result = NULL;
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    /* The inactive child remains exclusively owned while this small shared
     * stack reservation admits; unrelated private preparation stays parallel. */
    cpu_enable_interrupts();
    bool admitted = vm_kernel_owner_acquire(&owner);
    cpu_disable_interrupts();
    if (!admitted) {
      return MM_BUSY;
    }
  }
  enum mm_result status = prepare_user_task(process, entry, stack_top,
      preferred_cpu, result);
  vm_kernel_owner_release(&owner);
  return status;
}

void user_task_discard_prepared(struct task *task)
{
  KASSERT(task && task->kind == TASK_USER);
  process_prepare_require(task->process);
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    cpu_enable_interrupts();
    vm_kernel_owner_cleanup(&owner);
    cpu_disable_interrupts();
  }
  process_task_discard(task->process, &task->process_member);
  free_task(task);
  vm_kernel_owner_release(&owner);
}

static bool fs_context_valid(const struct private_context *context, uintptr_t base)
{
  return arch_user_fs_valid(base) && base >= context->usable_address &&
      base - context->usable_address <= context->usable_size;
}

static enum call_status prepare_thread_task(struct task *creator, const struct thread_create *input,
    struct thread_control *control, struct task **result)
{
  KASSERT(creator && creator->kind == TASK_USER && control);
  struct process *process = creator->process;
  process_vm_require_mutation(process);
  *result = NULL;
  if (!input->stack_identity || input->stack_identity == input->context_identity ||
      (!input->context_identity && input->fs_base) ||
      (input->context_identity && !arch_user_fs_valid(input->fs_base))) {
    return CALL_BAD_REQUEST;
  }
  struct page_translation code;
  if (vm_query(process->address_space, input->entry, &code) != MM_OK ||
      (code.permissions & (PAGE_USER | PAGE_EXEC)) != (PAGE_USER | PAGE_EXEC)) {
    return CALL_BAD_REQUEST;
  }
  struct task *task;
  enum mm_result allocation = allocate_task(&task);
  if (allocation != MM_OK) {
    return allocation == MM_BUSY ? CALL_BUSY : CALL_NO_MEMORY;
  }
  enum call_status status = private_context_pin(process, input->stack_identity,
      &task->stack_context);
  if (status == CALL_OK && input->context_identity) {
    status = private_context_pin(process, input->context_identity, &task->fs_context);
    if (status == CALL_OK && !fs_context_valid(&task->fs_context, input->fs_base)) {
      status = CALL_BAD_REQUEST;
    }
  }
  uintptr_t stack_top = task->stack_context.usable_address + task->stack_context.usable_size;
  if (status == CALL_OK && !arch_user_entry_valid(input->entry, stack_top)) {
    status = CALL_BAD_REQUEST;
  }
  if (status == CALL_OK) {
    task->request_storage = bsp_request_storage_create();
    task->profile = profile_storage_create();
    if (!task->request_storage || !task->profile) {
      status = CALL_NO_MEMORY;
    }
  }
  if (status == CALL_OK && !arch_user_state_init(&task->cpu)) {
    status = CALL_NO_MEMORY;
  }
  if (status != CALL_OK) {
    private_context_unpin(&task->stack_context);
    private_context_unpin(&task->fs_context);
    free_task(task);
    return status;
  }
  task->kind = TASK_USER;
  task->process = process;
  task->entry = input->entry;
  task->user_stack = stack_top;
  task->user_argument = input->argument;
  task->cpu_index = creator->cpu_index;
  task->thread_control = control;
  task->fs_bound = input->context_identity != 0;
  task->cpu.fs_base = input->fs_base;
  /* Kernel-root entry saved the creator state before preparation. */
  arch_user_state_inherit(&task->cpu, &creator->cpu);
  *result = task;
  return CALL_OK;
}

enum call_status task_thread_prepare(struct task *creator,
    const struct thread_create *input, struct thread_control *control,
    struct task **result)
{
  *result = NULL;
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    return CALL_BUSY;
  }
  enum call_status status = prepare_thread_task(creator, input, control, result);
  vm_kernel_owner_release(&owner);
  return status;
}

void task_thread_discard(struct task *task)
{
  KASSERT(task && !task->process_member.task);
  KASSERT(vm_kernel_owner_held());
  private_context_unpin(&task->stack_context);
  private_context_unpin(&task->fs_context);
  object_release(&task->thread_control->object);
  free_task(task);
}

enum call_status task_thread_attach_locked(struct task *task)
{
  return process_thread_attach_locked(task->process, &task->process_member, task);
}

void task_thread_enqueue(struct task *task)
{
  KASSERT(task->process_member.task == task);
  struct space *space = task->process->space;
  lock_queues();
  size_t cpu_index = place_locked(space, task->cpu_index);
  KASSERT(cpu_index != SIZE_MAX);
  task->cpu_index = cpu_index;
  space->setup_open = false;
  enqueue_locked(&schedulers[cpu_index], task);
  unlock_queues();
  /* Publication transferred task storage; use only the saved CPU index. */
  notify_remote_cpu(cpu_index);
}

enum call_status task_thread_bind_fs(struct task *task, uint64_t identity, uintptr_t base)
{
  KASSERT(task && task->kind == TASK_USER);
  process_vm_require_mutation(task->process);
  if (task->fs_bound) {
    return CALL_BUSY;
  }
  struct private_context context = {0};
  enum call_status status = private_context_pin(task->process, identity, &context);
  if (status != CALL_OK) {
    return status;
  }
  if (!fs_context_valid(&context, base)) {
    private_context_unpin(&context);
    return CALL_BAD_REQUEST;
  }
  spin_lock(&task->process->lifetime_lock);
  if (task->process->stopping || process_result_committed(task->process)) {
    status = CALL_ENDPOINT_CLOSED;
  } else {
    task->fs_context = context;
    task->fs_bound = true;
    task->cpu.fs_base = base;
  }
  spin_unlock(&task->process->lifetime_lock);
  if (status != CALL_OK) {
    private_context_unpin(&context);
  } else if (task == task_current_user()) {
    /* Keep the live FS register coherent with the retained state: leaving
     * kernel-root execution saves it again before restoring the private root. */
    arch_user_restore(&task->cpu);
  }
  return status;
}

void task_thread_exit_request(uint64_t value)
{
  struct task *task = task_current_user();
  KASSERT(task && task->in_syscall && !task->thread_exit_pending);
  task->thread_result = (struct thread_result){THREAD_EXITED, value};
  task->thread_exit_pending = true;
}

struct process *user_task_process(struct task *task)
{
  KASSERT(task && task->kind == TASK_USER);
  return task->process;
}

void user_task_publish_group(struct task **tasks, size_t count)
{
  KASSERT(count && count <= LAUNCH_BATCH_MAX);
  /* Published tasks may run and retire as soon as the lock drops, so keep
   * only their destination indices for notification. */
  size_t destinations[LAUNCH_BATCH_MAX];
  size_t destination_count = 0;
  for (size_t i = 0; i < count; ++i) {
    process_publish(user_task_process(tasks[i]));
  }
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
    /* Publication transfers prepared process lifetime and task storage. */
    user_task_publish_group(&task, 1);
  }
  return status;
}

static void complete_task(struct task *task)
{
  KASSERT(!task->bsp_request && !task->deferred_request);
  if (task->kind == TASK_USER) {
    process_task_depart(task->process, arch_cpu_index());
  }
  lock_queues();
  task->next = completed_head;
  completed_head = task;
  unlock_queues();
  /* The BSP can free task immediately after the unlock. Wake an idle BSP so
   * retirement does not wait for its next timer tick. */
  notify_remote_cpu(0);
}

/* Permanent BSP stack: detach one stop target, then transfer its owned storage.
 * No VM admission or storage destruction happens on this scheduler stack. */
static void reap_completed(void)
{
  lock_queues();
  struct task *task = completed_head;
  if (task) {
    completed_head = task->next;
    task->next = NULL;
  }
  unlock_queues();
  if (!task) {
    return;
  }
  if (task->kind == TASK_USER) {
    task->process->cpu_user_cycles += task->cpu_user_cycles;
    task->process->cpu_kernel_cycles += task->cpu_kernel_cycles;
    process_task_detach(task->process, &task->process_member);
    if (process_result_committed(task->process)) {
      process_request_stop(task->process);
    }
  }
  size_t cpu = task->cpu_index;
  lock_queues();
  task->next = ram_retired[cpu];
  ram_retired[cpu] = task;
  unlock_queues();
  process_ram_cleanup_notify(cpu);
}

bool task_ram_retire_pending(void)
{
  KASSERT(!cpu_interrupts_enabled());
  lock_queues();
  bool pending = ram_retired[arch_cpu_index()] != NULL;
  unlock_queues();
  return pending;
}

void task_reap_ram(void)
{
  KASSERT(cpu_interrupts_enabled());
  struct vm_kernel_owner owner = {0};
  uint64_t flags = cpu_save_interrupts();
  lock_queues();
  size_t cpu = arch_cpu_index();
  struct task *task = ram_retired[cpu];
  if (task) {
    ram_retired[cpu] = task->next;
    task->next = NULL;
  }
  unlock_queues();
  cpu_restore_interrupts(flags);
  if (!task) {
    return;
  }
  vm_kernel_owner_cleanup(&owner);
  flags = cpu_save_interrupts();
  struct process *process = task->process;
  struct execution_group *previous = object_cleanup_enter(process ?
      process->execution_group : task->cleanup_group);
  if (task->kind == TASK_USER) {
    struct private_context stack = task->stack_context, fs = task->fs_context;
    struct thread_control *control = task->thread_control;
    struct thread_result result = task->thread_result;
    bool faulted = task->faulted;
    task->stack_context = task->fs_context = (struct private_context){0};
    free_task(task);
    vm_kernel_owner_release(&owner);
    private_context_unpin(&stack);
    private_context_unpin(&fs);
    if (control) {
      if (!result.kind) {
        result = (struct thread_result){.kind = THREAD_STOPPED};
      }
      thread_control_complete(control, result);
      object_release(&control->object);
    }
    process_task_reclaimed(process);
    if (faulted) {
      kinfo("userspace: CPU %zu faulted task released\n", cpu);
    }
  } else {
    free_task(task);
    vm_kernel_owner_release(&owner);
  }
  object_cleanup_leave(previous);
  cpu_restore_interrupts(flags);
}

static void wake_sleepers(struct timer_clock *clock)
{
  if (!sleeping_tasks) {
    return;
  }
  uint64_t now = timer_clock_now(clock);
  while (sleeping_tasks && sleeping_tasks->sleep_deadline <= now) {
    struct task *task = sleeping_tasks;
    sleeping_tasks = task->next;
    task->sleep_deadline = 0;
    enqueue(&schedulers[0], task);
  }
}

void task_timer_interrupt(uint64_t now)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  clock_cache_update(now);
  if (!atomic_load_explicit(&started, memory_order_acquire)) {
    return;
  }
  struct timer_clock clock = {.taken = true, .now = now};
  expire_timed_waits(&clock);
  if (arch_cpu_index() == 0) {
    wake_sleepers(&clock);
  }
  arm_local_deadline(&clock);
}

void task_timer_service(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!atomic_load_explicit(&started, memory_order_acquire)) {
    return;
  }
  struct timer_clock clock = {};
  arm_local_deadline(&clock);
}

void kernel_task_sleep_until(uint64_t deadline)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && (flags & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_KERNEL && !task->exited);

  task->sleep_deadline = !deadline || task_deadline_expired(deadline) ? 0 : deadline;
  arch_context_switch(&task->saved_stack, scheduler->stack);
  cpu_restore_interrupts(flags);
}

void kernel_task_yield_if_runnable(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_KERNEL && !task->exited);
  KASSERT(arch_cpu_index() == 0 || task->ram_reaper);

  lock_queues();
  bool runnable = scheduler->ready_head != NULL;
  unlock_queues();
  if (runnable) {
    arch_context_switch(&task->saved_stack, scheduler->stack);
  }
  cpu_restore_interrupts(flags);
}

void kernel_task_yield_masked(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  cpu_enable_interrupts();
  kernel_task_yield_if_runnable();
  cpu_save_interrupts();
}

void task_yield_masked(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  /* Service interrupts between bounded steps, then relinquish this kernel
   * continuation without holding a user mapping or subsystem lock. */
  cpu_enable_interrupts();
  cpu_save_interrupts();
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && !task->exited && !task->vm_borrowers);
  KASSERT(task->kind == TASK_USER ? task->in_syscall :
      task->ram_reaper || arch_cpu_index() == 0);
  lock_queues();
  bool runnable = scheduler->ready_head != NULL ||
      scheduler->expired_address_waits != NULL ||
      (arch_cpu_index() == 0 && completed_head != NULL);
  unlock_queues();
  if (arch_cpu_index() == 0 && object_reap_pending()) {
    runnable = true;
  }
  if (runnable) {
    if (task->kind == TASK_USER) {
      arch_user_save(&task->cpu);
    }
    arch_context_switch(&task->saved_stack, scheduler->stack);
  }
}

[[noreturn]] void task_schedule(void)
{
  size_t cpu_index = arch_cpu_index();
  if (cpu_index == 0) {
    KASSERT(schedulers && !atomic_load_explicit(&started, memory_order_relaxed));
    vm_kernel_runtime_start();
    /* Publish scheduler storage and initialized console state to every AP. */
    atomic_store_explicit(&started, true, memory_order_release);
  } else {
    while (!atomic_load_explicit(&started, memory_order_acquire)) {
      cpu_wait_interrupt();
    }
  }

  struct scheduler *scheduler = local_scheduler();
  arch_timer_deadline_start();
  scheduler->cpu_mark = arch_tsc_read();
  for (;;) {
    task_timer_service();
    cancel_expired_address_waits(scheduler);
    if (cpu_index == 0) {
      reap_completed();
      object_reap();
    }

    struct task *task = dequeue(scheduler, cpu_index);
    scheduler->current_task = task;
    if (!task) {
      /* Remote ready-queue publication is followed by an IPI. With IF=0 here,
       * STI/HLT also handles a notification arriving after the empty check.
       * The timer still services deadlines and BSP-only request/cleanup queues. */
      cpu_wait_interrupt();
      uint64_t now = arch_tsc_read();
      scheduler->cpu_idle_cycles += now > scheduler->cpu_mark ? now - scheduler->cpu_mark : 0;
      scheduler->cpu_mark = now;
      continue;
    }

    if (task->kind == TASK_USER && !task->in_syscall && task_stop_requested()) {
      process_set_result(task->process,
          (struct process_result){.kind = PROCESS_TERMINATED});
      task->exited = true;
      scheduler->current_task = NULL;
      complete_task(task);
      continue;
    }
    if (hold_user_task(task)) {
      scheduler->current_task = NULL;
      continue;
    }
    bool private_root = task->kind == TASK_USER && !task->vm_kernel;
    struct process_vm_root_epoch root_epoch = {0};
    if (private_root && !process_vm_dispatch(task->process, task, &root_epoch)) {
      scheduler->current_task = NULL;
      continue;
    }

    /* Reload CR3 before touching a newly published task stack. This also
     * discards translations from a previous use of its kernel virtual range. */
    struct vm_space *address_space = private_root ?
                                    task->process->address_space : vm_kernel_space();
    KASSERT(vm_space_activate(address_space) == MM_OK);
    if (task->kind == TASK_USER) {
      arch_user_set_kernel_stack(task->kernel_stack + TASK_STACK_SIZE);
      arch_user_restore(&task->cpu);
    }
    /* One TSC read per dispatch: the return below closes this task's interval
     * and opens the next, so loop work in between is charged to the next task. */
    task->cpu_mark = scheduler->cpu_mark;
    arch_context_switch(&scheduler->stack, task->saved_stack);
    scheduler->cpu_mark = account_task(task, task->kind != TASK_USER || task->in_syscall);

    /* We are back on this CPU's permanent stack. Drop the private root and
     * flush translations before publishing completion to the BSP reaper. */
    KASSERT(!task->vm_borrowers);
    KASSERT(vm_space_activate(vm_kernel_space()) == MM_OK);
    arch_user_set_kernel_stack(0);
    scheduler->current_task = NULL;
    if (private_root) {
      process_vm_root_depart(task->process, root_epoch);
    }
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
      struct task **link = &sleeping_tasks;
      while (*link && (*link)->sleep_deadline <= task->sleep_deadline) {
        link = &(*link)->next;
      }
      task->next = *link;
      *link = task;
    } else if (!hold_user_task(task)) {
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
  lock_queues();
  bool schedule_needed = scheduler->ready_head != NULL ||
    scheduler->expired_address_waits != NULL ||
    (arch_cpu_index() == 0 &&
     completed_head != NULL);
  unlock_queues();
  if (task->kind == TASK_USER && atomic_load_explicit(&user_hold, memory_order_acquire)) {
    schedule_needed = true;
  }
  if (task->kind == TASK_USER && user_mode &&
      process_vm_execution_closed(task->process)) {
    schedule_needed = true;
  }
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
  finish_user_task((struct process_result){PROCESS_EXITED, status});
}

[[noreturn]] void user_start_failed(uint32_t reason)
{
  finish_user_task((struct process_result){PROCESS_START_FAILED, reason});
}

[[noreturn]] void user_fault(void)
{
  finish_user_task((struct process_result){.kind = PROCESS_FAULTED});
}
