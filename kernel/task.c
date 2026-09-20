#include <arch/cpu.h>
#include <arch/smp.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/object.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user.h>
#include <kernel/task.h>
#include <stdatomic.h>
#include <arch/cpu_local.h>

#define TASK_STACK_SIZE (16 * 1024)

enum task_kind {
  TASK_USER,
  TASK_KERNEL,
};

struct task {
  struct task *next;
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
  uint64_t sleep_start, sleep_ticks;
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

/* Sleeping kernel tasks belong to the BSP and are accessed only with IF=0. */
static struct task *sleeping_tasks;

/* IF=0 on every caller. Only list links cross CPUs; never allocate, log,
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

static void enqueue(struct scheduler *scheduler, struct task *task)
{
  lock_queues();
  task->next = NULL;
  if (scheduler->ready_tail) {
    scheduler->ready_tail->next = task;
  } else {
    scheduler->ready_head = task;
  }
  scheduler->ready_tail = task;
  unlock_queues();
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
    arch_enter_user(task->entry, task->user_stack);
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

enum mm_result user_task_create_on(size_t cpu_index, struct process *process,
                                   uintptr_t entry, uintptr_t stack_top)
{
  KASSERT(arch_cpu_index() == 0);
  if (!schedulers || cpu_index >= arch_cpu_count() || !process ||
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
  enum mm_result result = allocate_task(&task);
  if (result != MM_OK) {
    return result;
  }

  task->kind = TASK_USER;
  task->process = process;
  task->entry = entry;
  task->user_stack = stack_top;
  task->cpu_index = cpu_index;
  arch_user_state_init(&task->cpu);
  /* Publishing the queue link transfers the process and all private mappings.
   * The BSP must not touch the task or process again until completion. */
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
    if (task->kind == TASK_USER) {
      KASSERT(process_destroy(task->process) == MM_OK);
    }
    KASSERT(vm_free(vm_kernel_space(), task->kernel_stack, TASK_STACK_SIZE) == MM_OK);
    if (task->kind == TASK_USER) {
      if (task->faulted) {
        klog("userspace: CPU %zu faulted task released\n", task->cpu_index);
      } else {
        klog("userspace: CPU %zu exited with status %d; address space released\n",
             task->cpu_index, task->exit_status);
      }
    }
    kfree(task);
    task = next;
  }
}

static void wake_sleepers(void)
{
  uint64_t ticks = atomic_load_explicit(
      &cpu_current()->timer_interrupts, memory_order_relaxed);
  struct task **link = &sleeping_tasks;

  while (*link) {
    struct task *task = *link;
    if (ticks - task->sleep_start < task->sleep_ticks) {
      link = &task->next;
      continue;
    }

    *link = task->next;
    task->sleep_ticks = 0;
    enqueue(&schedulers[0], task);
  }
}

void kernel_task_sleep(uint64_t ticks)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && (flags & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_KERNEL && !task->exited);

  task->sleep_start = atomic_load_explicit(
      &cpu_current()->timer_interrupts, memory_order_relaxed);
  task->sleep_ticks = ticks;
  arch_context_switch(&task->saved_stack, scheduler->stack);
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
      reap_completed();
      object_reap();
      wake_sleepers();
    }

    struct task *task = dequeue(scheduler);
    scheduler->current_task = task;
    if (!task) {
      if (!idle_reported) {
        klog("scheduler: CPU %zu no runnable tasks, idle\n", cpu_index);
        idle_reported = true;
      }
      /* The local timer also bounds wakeup latency for cross-CPU submissions
       * and BSP cleanup, including a submission just before STI/HLT. */
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
    } else if (task->sleep_ticks) {
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
    wake_sleepers();
  }

  lock_queues();
  bool schedule_needed = scheduler->ready_head != NULL ||
    (arch_cpu_index() == 0 && completed_head != NULL);
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
