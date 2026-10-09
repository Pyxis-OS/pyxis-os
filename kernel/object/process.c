#include <abi/wait.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/process.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/process.h>
#include <kernel/user/wait.h>
#include <kernel/user_memory.h>

/* IF=0; lock order is control -> process lifetime -> scheduler queues. No allocation, user
 * copies or context switch under this lock. */
static void lock_control(struct process_control *control)
{
  while (atomic_exchange_explicit(&control->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_control(struct process_control *control)
{
  atomic_store_explicit(&control->locked, false, memory_order_release);
}

static void destroy_control(struct kernel_object *object)
{
  struct process_control *control = (struct process_control *)object;
  /* Each blocked observer retains a reference through its own handle. */
  KASSERT(!control->waiters && !control->process);
  kfree(control);
}

struct process_control *process_control_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct process_control *control = kmalloc(sizeof(*control));
  if (!control) {
    return NULL;
  }
  *control = (struct process_control){0};
  object_init(&control->object, OBJECT_PROCESS_CONTROL, destroy_control);
  atomic_init(&control->locked, false);
  return control;
}

void process_control_attach_process(struct process_control *control, struct process *process)
{
  KASSERT(arch_cpu_index() == 0 && process);
  lock_control(control);
  KASSERT(!control->process && !control->complete);
  control->process = process;
  unlock_control(control);
}

void process_control_detach_process(struct process_control *control)
{
  KASSERT(arch_cpu_index() == 0);
  lock_control(control);
  KASSERT(control->process && !control->complete);
  control->process = NULL;
  unlock_control(control);
}

void process_control_complete(struct process_control *control, struct process_result result)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(result.kind == PROCESS_EXITED || result.kind == PROCESS_FAULTED ||
      result.kind == PROCESS_TERMINATED);
  lock_control(control);
  KASSERT(!control->complete && !control->process);
  control->result = result;
  control->complete = true;

  while (control->waiters) {
    struct task_wait_link *waiter = control->waiters;
    control->waiters = waiter->next;
    waiter->next = NULL;
    struct task_wait *wait = waiter->wait;
    waiter->wait = NULL;
    /* Never access a detached record after wake: its task can immediately run. */
    task_wait_wake(wait);
  }
  unlock_control(control);
  readiness_notify();
}

uint64_t process_control_ready(struct process_control *control)
{
  uint64_t flags = cpu_save_interrupts();
  lock_control(control);
  bool complete = control->complete;
  unlock_control(control);
  cpu_restore_interrupts(flags);
  return complete ? WAIT_COMPLETE : 0;
}

struct syscall_result process_control_call(struct process_control *control,
    uint64_t rights, uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == PROCESS_TERMINATE) {
    if (!(rights & PROCESS_RIGHT_TERMINATE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (request_size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    /* The locked link keeps process storage live through stop targeting. */
    lock_control(control);
    if (control->process) {
      process_request_stop(control->process);
    }
    unlock_control(control);
    return (struct syscall_result){CALL_OK, 0};
  }
  if (operation != PROCESS_WAIT) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & PROCESS_RIGHT_WAIT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct process_result reply;
  if (request_size || reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct task_wait_link *waiter = task_wait_link_prepare();
  lock_control(control);
  if (task_stop_requested()) {
    unlock_control(control);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  if (!control->complete) {
    waiter->next = control->waiters;
    control->waiters = waiter;
    struct task_wait *wait = waiter->wait;
    unlock_control(control);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_control(control);
    if (waiter->wait) {
      struct task_wait_link **link = &control->waiters;
      while (*link != waiter) {
        KASSERT(*link);
        link = &(*link)->next;
      }
      *link = waiter->next;
      waiter->next = NULL;
      waiter->wait = NULL;
    }
    if (!resumed || task_stop_requested()) {
      unlock_control(control);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
  }
  KASSERT(control->complete);
  reply = control->result;
  unlock_control(control);

  /* Only the resumed caller touches its user mapping. Completion is not
   * consumed, so subsequent waits and other observers receive the same value. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
