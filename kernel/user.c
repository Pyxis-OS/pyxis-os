#include <arch/cpu.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/user.h>

#define USER_KERNEL_STACK_SIZE (16 * 1024)

struct user_task {
  struct user_task *next;
  struct vm_space *space;
  uintptr_t kernel_stack;
  uintptr_t saved_stack;
  uintptr_t entry, user_stack;
  struct arch_user_state cpu;
  bool exited, faulted;
  int exit_status;
};

static struct user_task *ready_head, *ready_tail;
static struct user_task *current_task;
static uintptr_t scheduler_stack;
static bool started;

static void enqueue(struct user_task *task)
{
  task->next = NULL;
  if (ready_tail) {
    ready_tail->next = task;
  } else {
    ready_head = task;
  }
  ready_tail = task;
}

static struct user_task *dequeue(void)
{
  struct user_task *task = ready_head;
  if (task) {
    ready_head = task->next;
    if (!ready_head) {
      ready_tail = NULL;
    }
    task->next = NULL;
  }
  return task;
}

[[noreturn]] static void enter_task(void)
{
  arch_enter_user(current_task->entry, current_task->user_stack);
}

enum mm_result user_task_create(struct vm_space *space, uintptr_t entry,
                                uintptr_t stack_top)
{
  if (!space || space == vm_kernel_space() || !arch_user_entry_valid(entry, stack_top)) {
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
  arch_user_state_init(&task->cpu);
  task->saved_stack = arch_context_prepare(task->kernel_stack + USER_KERNEL_STACK_SIZE,
                                           enter_task);
  enqueue(task);
  return MM_OK;
}

static void reap_task(struct user_task *task)
{
  /* Both the active root and the executing stack now belong to the scheduler. */
  KASSERT(vm_space_destroy(task->space) == MM_OK);
  KASSERT(vm_free(vm_kernel_space(), task->kernel_stack, USER_KERNEL_STACK_SIZE) == MM_OK);
  if (task->faulted) {
    klog("userspace: faulted task released\n");
  } else {
    klog("userspace: exited with status %d; address space released\n", task->exit_status);
  }
  kfree(task);
}

[[noreturn]] void user_schedule(void)
{
  KASSERT(!started);
  started = true;
  bool idle_reported = false;

  for (;;) {
    current_task = dequeue();
    if (!current_task) {
      if (!idle_reported) {
        klog("userspace: no runnable tasks, idle\n");
        idle_reported = true;
      }
      cpu_wait_interrupt();
      continue;
    }
    idle_reported = false;

    KASSERT(vm_space_activate(current_task->space) == MM_OK);
    arch_user_set_kernel_stack(current_task->kernel_stack + USER_KERNEL_STACK_SIZE);
    arch_user_restore(&current_task->cpu);
    arch_context_switch(&scheduler_stack, current_task->saved_stack);

    KASSERT(vm_space_activate(vm_kernel_space()) == MM_OK);
    arch_user_set_kernel_stack(0);
    struct user_task *task = current_task;
    current_task = NULL;
    if (task->exited) {
      reap_task(task);
    } else {
      enqueue(task);
    }
  }
}

void user_preempt(void)
{
  KASSERT(current_task && !current_task->exited);
  if (!ready_head) {
    return;
  }

  arch_user_save(&current_task->cpu);
  arch_context_switch(&current_task->saved_stack, scheduler_stack);
}

[[noreturn]] void user_exit(int status)
{
  KASSERT(current_task && !current_task->exited);
  current_task->exit_status = status;
  current_task->exited = true;
  arch_context_switch(&current_task->saved_stack, scheduler_stack);
  panic("resumed an exited user task");
}

[[noreturn]] void user_fault(void)
{
  KASSERT(current_task);
  current_task->faulted = true;
  user_exit(-1);
}
