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
  bool exited, faulted;
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

bool kernel_task_is_current(void (*entry)(void *))
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  return task && task->kind == TASK_KERNEL && task->kernel_entry == entry;
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

static void sleep_wait(struct task_wait *wait, bool timed, uint64_t deadline)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && wait == &task->wait_record && wait->task == task && !task->exited);
  KASSERT(task->kind == TASK_USER || arch_cpu_index() == 0);

  lock_queues();
  if (wait->notified || (timed && task_deadline_expired(deadline))) {
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
  sleep_wait(wait, false, 0);
}

void task_wait_sleep_until(struct task_wait *wait, uint64_t deadline)
{
  sleep_wait(wait, true, deadline);
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

void task_wait_wake(struct task_wait *wait)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  if (wait->timed) {
    struct task_wait **link = &timed_waits;
    while (*link != wait) {
      KASSERT(*link);
      link = &(*link)->timeout_next;
    }
    *link = wait->timeout_next;
    wait->timeout_next = NULL;
    wait->timed = false;
  }
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

static struct task *dequeue(struct scheduler *scheduler)
{
  lock_queues();
  struct task *task = scheduler->ready_head;
  if (task) {
    scheduler->ready_head = task->next;
    if (!scheduler->ready_head) {
      scheduler->ready_tail = NULL;
    }
    task->next = NULL;
  }
  unlock_queues();
  return task;
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

enum mm_result user_task_prepare_on(size_t cpu_index, struct process *process,
                                    uintptr_t entry, uintptr_t stack_top,
                                    struct task **result)
{
  KASSERT(arch_cpu_index() == 0);
  *result = NULL;
  if (!schedulers || cpu_index >= arch_cpu_count() || !process ||
      !process->startup_address ||
      process->space != arch_cpu_at(cpu_index)->space ||
      !arch_user_entry_valid(entry, stack_top)) {
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
  task->cpu_index = cpu_index;
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
  size_t cpu_index = tasks[0]->cpu_index;
  lock_queues();
  for (size_t i = 0; i < count; ++i) {
    KASSERT(tasks[i] && tasks[i]->cpu_index == cpu_index);
    enqueue_locked(&schedulers[cpu_index], tasks[i]);
  }
  unlock_queues();
  notify_remote_cpu(cpu_index);
}

enum mm_result user_task_create_on(size_t cpu_index, struct process *process,
                                   uintptr_t entry, uintptr_t stack_top)
{
  struct task *task;
  enum mm_result status = user_task_prepare_on(cpu_index, process, entry,
      stack_top, &task);
  if (status != MM_OK) {
    return status;
  }
  /* Publication transfers process and stack ownership. */
  enqueue(&schedulers[cpu_index], task);
  return MM_OK;
}

enum mm_result user_task_create(struct process *process, uintptr_t entry,
                                uintptr_t stack_top)
{
  return user_task_create_on(0, process, entry, stack_top);
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
      /* Transfer the execution owner's reference before freeing the process. */
      control = task->process->control;
      task->process->control = NULL;
      execution_group = task->process->execution_group;
      task->process->execution_group = NULL;
      result.kind = task->faulted ? PROCESS_FAULTED : PROCESS_EXITED;
      result.exit_status = task->faulted ? 0 : task->exit_status;
      KASSERT(process_destroy(task->process) == MM_OK);
    }
    if (task->kind == TASK_USER) {
      if (task->faulted) {
        klog("userspace: CPU %zu faulted task released\n", task->cpu_index);
      } else {
        klog("userspace: CPU %zu exited with status %d; address space released\n",
             task->cpu_index, task->exit_status);
      }
    }
    free_task(task);
    if (execution_group) {
      execution_group_member_complete(execution_group);
    }
    if (control) {
      process_control_complete(control, result);
      object_release(&control->object);
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

    struct task *task = dequeue(scheduler);
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
      enqueue(scheduler, task);
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
