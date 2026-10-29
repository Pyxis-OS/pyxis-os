#include <kernel/object/clipboard.h>
#include <abi/wait.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/process.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/process.h>
#include <kernel/user/wait.h>
#include <kernel/user/readiness_source.h>
#include <kernel/user_memory.h>

/* The control reference outlives its borrowed process link. All notification
 * state and logical-grant accounting use the control lock. */
struct process_interrupt {
  struct kernel_object object;
  struct process_control *control;
  struct readiness_source readiness;
  size_t receivers;
  bool pending;
  bool closed;
};

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

static void notify_interrupt(struct process_interrupt *receiver)
{
  readiness_source_notify(&receiver->readiness);
  readiness_notify();
}

static void close_interrupt_locked(struct process_interrupt *receiver)
{
  if (receiver->control->interrupt == receiver) {
    receiver->control->interrupt = NULL;
  }
  receiver->closed = true;
  receiver->pending = false;
}

static void destroy_control(struct kernel_object *object)
{
  struct process_control *control = (struct process_control *)object;
  /* Each blocked observer retains a reference through its own handle. */
  KASSERT(!control->waiters && !control->process && !control->interrupt);
  kfree(control);
}

struct process_control *process_control_create(void)
{
  KASSERT(!cpu_interrupts_enabled());
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
  KASSERT(!cpu_interrupts_enabled() && process);
  process_prepare_require(process);
  lock_control(control);
  KASSERT(!control->process && !control->complete);
  control->process = process;
  unlock_control(control);
}

void process_control_detach_process(struct process_control *control)
{
  KASSERT(!cpu_interrupts_enabled());
  lock_control(control);
  KASSERT(control->process && !control->complete);
  /* No owner task remains at retirement; its readiness sets are already
   * closing. Clearing this weak link needs no surviving owner notification. */
  if (control->interrupt) {
    close_interrupt_locked(control->interrupt);
  }
  control->process = NULL;
  unlock_control(control);
}

void process_control_complete(struct process_control *control, struct process_result result)
{
  KASSERT(!cpu_interrupts_enabled());
  KASSERT(result.kind == PROCESS_EXITED || result.kind == PROCESS_FAULTED ||
      result.kind == PROCESS_TERMINATED || result.kind == PROCESS_START_FAILED);
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

bool process_control_stopped(struct process_control *control)
{
  if (!control) {
    return true;
  }
  lock_control(control);
  struct process *process = control->process;
  bool stopped = !process;
  if (process) {
    spin_lock(&process->lifetime_lock);
    stopped = process->stopping || process->lifetime == PROCESS_RETIRED ||
        process_result_committed(process);
    spin_unlock(&process->lifetime_lock);
  }
  unlock_control(control);
  return stopped;
}

static void destroy_interrupt(struct kernel_object *object)
{
  struct process_interrupt *receiver = (struct process_interrupt *)object;
  lock_control(receiver->control);
  KASSERT(!receiver->receivers && receiver->control->interrupt != receiver);
  unlock_control(receiver->control);
  object_release(&receiver->control->object);
  kfree(receiver);
}

void process_interrupt_create_execute(struct process_interrupt_create_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  struct process_control *control = request->caller->control;
  request->created = NULL;
  if (task_wait_stop_requested(request->request.wait) || process_control_stopped(control)) {
    request->status = CALL_ENDPOINT_CLOSED;
    return;
  }
  struct process_interrupt *receiver = kmalloc(sizeof(*receiver));
  if (!receiver) {
    request->status = CALL_NO_MEMORY;
    return;
  }
  if (!object_retain(&control->object)) {
    kfree(receiver);
    request->status = CALL_LIMIT;
    return;
  }
  *receiver = (struct process_interrupt){.control = control};
  object_init(&receiver->object, OBJECT_PROCESS_INTERRUPT, destroy_interrupt);
  request->created = &receiver->object;
  request->status = CALL_OK;
}

enum call_status process_interrupt_publish(struct kernel_object *object, struct process *caller)
{
  struct process_interrupt *receiver = (struct process_interrupt *)object;
  struct process_control *control = receiver->control;
  lock_control(control);
  enum call_status status = CALL_ENDPOINT_CLOSED;
  if (control->process == caller && !receiver->closed && receiver->receivers) {
    spin_lock(&caller->lifetime_lock);
    if (!caller->stopping && !process_result_committed(caller)) {
      status = control->interrupt ? CALL_BUSY : CALL_OK;
      if (status == CALL_OK) {
        control->interrupt = receiver;
      }
    }
    spin_unlock(&caller->lifetime_lock);
  }
  unlock_control(control);
  return status;
}

void process_interrupt_abandon(struct kernel_object *object)
{
  struct process_interrupt *receiver = (struct process_interrupt *)object;
  lock_control(receiver->control);
  close_interrupt_locked(receiver);
  unlock_control(receiver->control);
  notify_interrupt(receiver);
}

bool process_interrupt_grant_retain(struct kernel_object *object, uint64_t rights)
{
  if (!(rights & PROCESS_INTERRUPT_RIGHT_RECEIVE)) {
    return true;
  }
  struct process_interrupt *receiver = (struct process_interrupt *)object;
  lock_control(receiver->control);
  bool result = !receiver->closed && receiver->receivers != SIZE_MAX;
  if (result) {
    ++receiver->receivers;
  }
  unlock_control(receiver->control);
  return result;
}

void process_interrupt_grant_release(struct kernel_object *object, uint64_t rights)
{
  if (!(rights & PROCESS_INTERRUPT_RIGHT_RECEIVE)) {
    return;
  }
  struct process_interrupt *receiver = (struct process_interrupt *)object;
  lock_control(receiver->control);
  KASSERT(receiver->receivers);
  bool last = !--receiver->receivers;
  if (last) {
    close_interrupt_locked(receiver);
  }
  unlock_control(receiver->control);
  if (last) {
    /* The released grant still retains storage until this hook returns. */
    notify_interrupt(receiver);
  }
}

bool process_interrupt_owned(const struct kernel_object *object, struct process *caller)
{
  const struct process_interrupt *receiver = (const struct process_interrupt *)object;
  uint64_t flags = cpu_save_interrupts();
  lock_control(receiver->control);
  bool owned = receiver->control->process == caller;
  unlock_control(receiver->control);
  cpu_restore_interrupts(flags);
  return owned;
}

uint64_t process_interrupt_ready(struct kernel_object *object)
{
  struct process_interrupt *receiver = (struct process_interrupt *)object;
  uint64_t flags = cpu_save_interrupts();
  lock_control(receiver->control);
  uint64_t ready = receiver->closed ? WAIT_ERROR : receiver->pending ? WAIT_READABLE : 0;
  unlock_control(receiver->control);
  cpu_restore_interrupts(flags);
  return ready;
}

struct readiness_source *process_interrupt_readiness_source(struct kernel_object *object)
{
  return &((struct process_interrupt *)object)->readiness;
}

struct syscall_result process_interrupt_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t request_size)
{
  if (operation != PROCESS_INTERRUPT_TAKE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & PROCESS_INTERRUPT_RIGHT_RECEIVE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!task_operation_start()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct process_interrupt *receiver = (struct process_interrupt *)object;
  struct process_control *control = receiver->control;
  lock_control(control);
  enum call_status status;
  if (control->process != process_current()) {
    status = CALL_DENIED;
  } else if (receiver->closed || control->interrupt != receiver) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (!receiver->pending) {
    status = CALL_WOULD_BLOCK;
  } else {
    receiver->pending = false;
    status = CALL_OK;
  }
  unlock_control(control);
  if (status == CALL_OK) {
    notify_interrupt(receiver);
  }
  return (struct syscall_result){status, 0};
}

static enum call_status request_interrupt(struct process_control *control)
{
  struct process_interrupt *receiver = NULL;
  enum call_status status = CALL_ENDPOINT_CLOSED;
  lock_control(control);
  struct process *process = control->process;
  if (process) {
    spin_lock(&process->lifetime_lock);
    if (!process->stopping && !process_result_committed(process)) {
      receiver = control->interrupt;
      if (!receiver) {
        status = CALL_NOT_FOUND;
      } else if (!object_retain(&receiver->object)) {
        receiver = NULL;
        status = CALL_LIMIT;
      } else {
        receiver->pending = true;
        status = CALL_OK;
      }
    }
    spin_unlock(&process->lifetime_lock);
  }
  unlock_control(control);
  if (receiver) {
    notify_interrupt(receiver);
    object_release(&receiver->object);
  }
  return status;
}

struct syscall_result process_control_call(struct process_control *control,
    uint64_t rights, uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == PROCESS_TERMINATE || operation == PROCESS_INTERRUPT) {
    if (!(rights & PROCESS_RIGHT_TERMINATE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (request_size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (operation == PROCESS_INTERRUPT) {
      return (struct syscall_result){request_interrupt(control), 0};
    }
    /* The locked link keeps process storage live through stop targeting. */
    lock_control(control);
    if (control->process) {
      process_request_stop(control->process);
    }
    unlock_control(control);
    clipboard_stop_notify();
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
  if (task_stop_requested() || task_operation_denied()) {
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
    if (!resumed || (task_stop_requested() || task_operation_denied())) {
      unlock_control(control);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
  }
  KASSERT(control->complete);
  reply = control->result;
  unlock_control(control);

  /* Only the resumed caller touches its user mapping. Completion is not
   * consumed, so subsequent waits and other observers receive the same value. */
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
