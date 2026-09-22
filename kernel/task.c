#include <abi/memory.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/fs/ramfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/mm/vm.h>
#include <kernel/object/object.h>
#include <kernel/object/file.h>
#include <kernel/object/process.h>
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

struct task_wait {
  struct task *task;
  bool notified;
};

struct task {
  struct task *next;
  struct task *growth_next;
  enum capability_result growth_result;
  struct task *directory_next;
  struct directory_entry *directory_entry;
  uint64_t directory_kind;
  size_t directory_name_length;
  bool directory_discard;
  struct file_wait file_wait;
  struct process_wait process_wait;
  struct task *file_next;
  struct file_object *file;
  size_t file_capacity;
  bool file_result;
  struct task *memory_next;
  struct memory_region memory_region;
  uint64_t memory_operation;
  enum mm_result memory_result;
  bool memory_pending; /* Awaiting scheduler publication after leaving the private root. */
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
static struct task *growth_head, *growth_tail;
static struct task *directory_head, *directory_tail;
static struct task *file_head, *file_tail;
static struct task *memory_head, *memory_tail;
static atomic_bool started;
static atomic_bool queues_locked;

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

static void enqueue(struct scheduler *scheduler, struct task *task)
{
  lock_queues();
  enqueue_locked(scheduler, task);
  unlock_queues();
}

struct task_wait *task_wait_prepare(void)
{
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER && !task->exited);
  KASSERT(!task->wait && !task->parked);
  task->wait_record = (struct task_wait){.task = task};
  return &task->wait_record;
}

struct process_wait *task_prepare_process_wait(void)
{
  struct task_wait *wait = task_wait_prepare();
  struct task *task = wait->task;
  task->process_wait = (struct process_wait){.wait = wait};
  return &task->process_wait;
}

void task_wait_sleep(struct task_wait *wait)
{
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task == wait->task && task->kind == TASK_USER);

  lock_queues();
  if (wait->notified) {
    unlock_queues();
    return;
  }
  KASSERT(!task->wait && !task->parked);
  task->wait = wait;
  unlock_queues();

  arch_user_save(&task->cpu);
  arch_context_switch(&task->saved_stack, scheduler->stack);
}

void task_wait_wake(struct task_wait *wait)
{
  lock_queues();
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
  unlock_queues();
}

enum capability_result task_grow_capabilities(void)
{
  struct task_wait *wait = task_wait_prepare();
  struct task *task = wait->task;

  lock_queues();
  task->growth_next = NULL;
  if (growth_tail) {
    growth_tail->growth_next = task;
  } else {
    growth_head = task;
  }
  growth_tail = task;
  unlock_queues();

  /* Do not touch the table after publication. The BSP may finish before
   * sleep; the wait record preserves that early completion. */
  task_wait_sleep(wait);
  return task->growth_result;
}

static void grow_requested_tables(void)
{
  lock_queues();
  struct task *task = growth_head;
  growth_head = growth_tail = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->growth_next;
    task->growth_result = capability_grow(&task->process->capabilities);
    task_wait_wake(&task->wait_record);
    /* Waking returns table ownership; the task may immediately exit. */
    task = next;
  }
}

static void queue_directory_request(struct task *task, struct task_wait *wait)
{
  lock_queues();
  task->directory_next = NULL;
  if (directory_tail) {
    directory_tail->directory_next = task;
  } else {
    directory_head = task;
  }
  directory_tail = task;
  unlock_queues();
  task_wait_sleep(wait);
}

struct directory_entry *task_allocate_directory_entry(uint64_t kind, size_t name_length)
{
  struct task_wait *wait = task_wait_prepare();
  struct task *task = wait->task;
  task->directory_kind = kind;
  task->directory_name_length = name_length;
  task->directory_discard = false;
  task->directory_entry = NULL;
  queue_directory_request(task, wait);
  struct directory_entry *entry = task->directory_entry;
  task->directory_entry = NULL;
  return entry;
}

void task_discard_directory_entry(struct directory_entry *entry)
{
  struct task_wait *wait = task_wait_prepare();
  struct task *task = wait->task;
  task->directory_discard = true;
  task->directory_entry = entry;
  queue_directory_request(task, wait);
}

static void service_directory_requests(void)
{
  lock_queues();
  struct task *task = directory_head;
  directory_head = directory_tail = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->directory_next;
    if (task->directory_discard) {
      ramfs_discard_entry(task->directory_entry);
      task->directory_entry = NULL;
    } else {
      task->directory_entry = ramfs_allocate_entry(task->directory_kind,
          task->directory_name_length);
    }
    task_wait_wake(&task->wait_record);
    /* All result storage is published before waking; task may now exit. */
    task = next;
  }
}

struct file_wait *task_prepare_file_wait(void)
{
  struct task_wait *wait = task_wait_prepare();
  struct file_wait *record = &wait->task->file_wait;
  *record = (struct file_wait){.wait = wait};
  return record;
}

bool task_replace_file_buffer(struct file_object *file, size_t capacity)
{
  struct task_wait *wait = task_wait_prepare();
  struct task *task = wait->task;
  task->file = file;
  task->file_capacity = capacity;

  lock_queues();
  task->file_next = NULL;
  if (file_tail) {
    file_tail->file_next = task;
  } else {
    file_head = task;
  }
  file_tail = task;
  unlock_queues();

  task_wait_sleep(wait);
  task->file = NULL;
  return task->file_result;
}

static void service_file_requests(void)
{
  lock_queues();
  struct task *task = file_head;
  file_head = file_tail = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->file_next;
    task->file_result = file_replace_buffer(task->file, task->file_capacity);
    task_wait_wake(&task->wait_record);
    /* The requester owns the file again and may immediately exit. */
    task = next;
  }
}

enum mm_result task_request_memory(uint64_t operation, struct memory_region *region)
{
  KASSERT(operation == MEMORY_ALLOCATE || operation == MEMORY_RELEASE);
  struct task_wait *wait = task_wait_prepare();
  struct task *task = wait->task;
  KASSERT(!task->memory_pending);
  task->memory_operation = operation;
  task->memory_region = *region;
  task->memory_pending = true;

  /* Unlike heap requests, VM ownership cannot be lent while this CPU still
   * runs on the private root. The scheduler publishes after the switch. */
  task_wait_sleep(wait);
  *region = task->memory_region;
  return task->memory_result;
}

static void publish_memory_request(struct task *task)
{
  lock_queues();
  KASSERT(task->wait == &task->wait_record && !task->wait->notified);
  KASSERT(task->memory_pending && !task->parked);
  task->memory_pending = false;
  task->parked = true;
  task->memory_next = NULL;
  if (memory_tail) {
    memory_tail->memory_next = task;
  } else {
    memory_head = task;
  }
  memory_tail = task;
  unlock_queues();
  /* Ownership is now with BSP; do not touch task or its private VM again. */
}

static void service_memory_requests(void)
{
  lock_queues();
  struct task *task = memory_head;
  memory_head = memory_tail = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->memory_next;
    KASSERT(task->parked && task->wait == &task->wait_record);
    struct memory_region *region = &task->memory_region;
    if (task->memory_operation == MEMORY_ALLOCATE) {
      task->memory_result = private_memory_allocate(task->process, region->size,
          &region->address);
    } else {
      KASSERT(task->memory_operation == MEMORY_RELEASE);
      task->memory_result = private_memory_release(task->process, region->address,
          region->size);
    }
    task_wait_wake(&task->wait_record);
    /* Resumption reloads CR3 before returning to the private task stack. */
    task = next;
  }
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
    struct process_control *control = NULL;
    struct process_result result = {0};
    if (task->kind == TASK_USER) {
      /* Transfer the execution owner's reference before freeing the process. */
      control = task->process->control;
      task->process->control = NULL;
      result.kind = task->faulted ? PROCESS_FAULTED : PROCESS_EXITED;
      result.exit_status = task->faulted ? 0 : task->exit_status;
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
    if (control) {
      process_control_complete(control, result);
      object_release(&control->object);
    }
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
      grow_requested_tables();
      service_directory_requests();
      service_file_requests();
      service_memory_requests();
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
    } else if (task->memory_pending) {
      publish_memory_request(task);
    } else if (task->wait) {
      lock_queues();
      if (task->wait->notified) {
        task->wait = NULL;
        enqueue_locked(scheduler, task);
      } else {
        task->parked = true;
      }
      unlock_queues();
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
    (arch_cpu_index() == 0 &&
     (completed_head != NULL || growth_head != NULL || directory_head != NULL ||
      file_head != NULL || memory_head != NULL));
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
