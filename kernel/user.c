#include <arch/cpu.h>
#include <arch/smp.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/user.h>
#include <stdatomic.h>

#define USER_KERNEL_STACK_SIZE (16 * 1024)

struct user_task {
  struct user_task *next;
  struct vm_space *space;
  uintptr_t kernel_stack;
  uintptr_t saved_stack;
  uintptr_t entry, user_stack;
  struct arch_user_state cpu;
  size_t cpu_index;
  bool exited, faulted;
  int exit_status;
};

struct user_scheduler {
  struct user_task *ready_head, *ready_tail;
  struct user_task *current_task;
  uintptr_t stack;
};

static struct user_scheduler *schedulers;
static struct user_task *completed_head;
static atomic_bool started;
static atomic_bool queues_locked;

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

static struct user_scheduler *local_scheduler(void)
{
  return &schedulers[arch_cpu_index()];
}

void user_init(void)
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

static void enqueue(struct user_scheduler *scheduler, struct user_task *task)
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

static struct user_task *dequeue(struct user_scheduler *scheduler)
{
  lock_queues();
  struct user_task *task = scheduler->ready_head;
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
  struct user_task *task = local_scheduler()->current_task;
  arch_enter_user(task->entry, task->user_stack);
}

enum mm_result user_task_create_on(size_t cpu_index, struct vm_space *space,
                                   uintptr_t entry, uintptr_t stack_top)
{
  KASSERT(arch_cpu_index() == 0);
  if (!schedulers || cpu_index >= arch_cpu_count() || !space ||
      space == vm_kernel_space() || !arch_user_entry_valid(entry, stack_top)) {
    return MM_INVALID;
  }

  struct page_translation code, stack;
  if (vm_query(space, entry, &code) != MM_OK ||
      vm_query(space, stack_top - 1, &stack) != MM_OK ||
      (code.permissions & (PAGE_USER | PAGE_EXEC)) != (PAGE_USER | PAGE_EXEC) ||
      (stack.permissions & (PAGE_USER | PAGE_WRITE)) != (PAGE_USER | PAGE_WRITE)) {
    return MM_INVALID;
  }

  struct user_task *task = kmalloc(sizeof(*task));
  if (!task) {
    return MM_NO_MEMORY;
  }
  memset(task, 0, sizeof(*task));

  enum mm_result result = vm_alloc(vm_kernel_space(), USER_KERNEL_STACK_SIZE,
                                    PAGE_SIZE, PAGE_WRITE, &task->kernel_stack);
  if (result != MM_OK) {
    kfree(task);
    return result;
  }

  task->space = space;
  task->entry = entry;
  task->user_stack = stack_top;
  task->cpu_index = cpu_index;
  arch_user_state_init(&task->cpu);
  task->saved_stack = arch_context_prepare(task->kernel_stack + USER_KERNEL_STACK_SIZE,
                                           enter_task);
  /* Publishing the queue link transfers ownership, including all mappings.
   * The BSP must not touch the task or its space again until completion. */
  enqueue(&schedulers[cpu_index], task);
  return MM_OK;
}

enum mm_result user_task_create(struct vm_space *space, uintptr_t entry,
                                uintptr_t stack_top)
{
  return user_task_create_on(0, space, entry, stack_top);
}

static void complete_task(struct user_task *task)
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
  struct user_task *task = completed_head;
  completed_head = NULL;
  unlock_queues();

  while (task) {
    struct user_task *next = task->next;
    KASSERT(vm_space_destroy(task->space) == MM_OK);
    KASSERT(vm_free(vm_kernel_space(), task->kernel_stack, USER_KERNEL_STACK_SIZE) == MM_OK);
    if (task->faulted) {
      klog("userspace: CPU %zu faulted task released\n", task->cpu_index);
    } else {
      klog("userspace: CPU %zu exited with status %d; address space released\n",
           task->cpu_index, task->exit_status);
    }
    kfree(task);
    task = next;
  }
}

[[noreturn]] void user_schedule(void)
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

  struct user_scheduler *scheduler = local_scheduler();
  bool idle_reported = false;
  for (;;) {
    if (cpu_index == 0) {
      reap_completed();
    }

    struct user_task *task = dequeue(scheduler);
    scheduler->current_task = task;
    if (!task) {
      if (!idle_reported) {
        klog("userspace: CPU %zu no runnable tasks, idle\n", cpu_index);
        idle_reported = true;
      }
      /* The local timer also bounds wakeup latency for cross-CPU submissions
       * and BSP cleanup, including a submission just before STI/HLT. */
      cpu_wait_interrupt();
      continue;
    }
    idle_reported = false;

    /* Reload CR3 before touching a newly published task stack. This also
     * discards translations from a previous use of its kernel virtual range. */
    KASSERT(vm_space_activate(task->space) == MM_OK);
    arch_user_set_kernel_stack(task->kernel_stack + USER_KERNEL_STACK_SIZE);
    arch_user_restore(&task->cpu);
    arch_context_switch(&scheduler->stack, task->saved_stack);

    /* We are back on this CPU's permanent stack. Drop the private root and
     * flush translations before publishing completion to the BSP reaper. */
    KASSERT(vm_space_activate(vm_kernel_space()) == MM_OK);
    arch_user_set_kernel_stack(0);
    scheduler->current_task = NULL;
    if (task->exited) {
      complete_task(task);
    } else {
      enqueue(scheduler, task);
    }
  }
}

void user_preempt(void)
{
  struct user_scheduler *scheduler = local_scheduler();
  struct user_task *task = scheduler->current_task;
  KASSERT(task && !task->exited);

  lock_queues();
  bool schedule_needed = scheduler->ready_head != NULL ||
    (arch_cpu_index() == 0 && completed_head != NULL);
  unlock_queues();
  if (!schedule_needed) {
    return;
  }

  arch_user_save(&task->cpu);
  arch_context_switch(&task->saved_stack, scheduler->stack);
}

[[noreturn]] void user_exit(int status)
{
  struct user_scheduler *scheduler = local_scheduler();
  struct user_task *task = scheduler->current_task;
  KASSERT(task && !task->exited);
  task->exit_status = status;
  task->exited = true;
  arch_context_switch(&task->saved_stack, scheduler->stack);
  panic("resumed an exited user task");
}

[[noreturn]] void user_fault(void)
{
  struct user_task *task = local_scheduler()->current_task;
  KASSERT(task);
  task->faulted = true;
  user_exit(-1);
}
