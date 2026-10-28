#include <kernel/mm/kernel_owner.h>
#include <kernel/object/forwarder.h>
#include <abi/wait.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/object/thread.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user/address_wait.h>
#include <kernel/user_memory.h>
#include <kernel/panic.h>

static void lock_control(struct thread_control *control)
{
  while (atomic_exchange_explicit(&control->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_control(struct thread_control *control)
{
  atomic_store_explicit(&control->locked, false, memory_order_release);
}

static void destroy_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *thread_service_create(void)
{
  KASSERT(!cpu_interrupts_enabled());
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_THREAD_SERVICE, destroy_service);
  }
  return object;
}

static void destroy_control(struct kernel_object *object)
{
  struct thread_control *control = (struct thread_control *)object;
  KASSERT(!control->waiters);
  kfree(control);
}

struct thread_control *thread_control_create(void)
{
  KASSERT(!cpu_interrupts_enabled());
  struct thread_control *control = kmalloc(sizeof(*control));
  if (control) {
    *control = (struct thread_control){0};
    object_init(&control->object, OBJECT_THREAD_COMPLETION, destroy_control);
    atomic_init(&control->locked, false);
  }
  return control;
}

void thread_control_complete(struct thread_control *control, struct thread_result result)
{
  KASSERT(!cpu_interrupts_enabled() &&
      (result.kind == THREAD_EXITED || result.kind == THREAD_STOPPED));
  lock_control(control);
  KASSERT(!control->complete);
  control->result = result;
  control->complete = true;
  while (control->waiters) {
    struct task_wait_link *link = control->waiters;
    control->waiters = link->next;
    link->next = NULL;
    struct task_wait *wait = link->wait;
    link->wait = NULL;
    task_wait_wake(wait);
  }
  unlock_control(control);
  readiness_notify();
}

uint64_t thread_control_ready(struct thread_control *control)
{
  uint64_t flags = cpu_save_interrupts();
  lock_control(control);
  bool complete = control->complete;
  unlock_control(control);
  cpu_restore_interrupts(flags);
  return complete ? WAIT_COMPLETE : 0;
}

struct syscall_result thread_control_call(struct thread_control *control,
    uint64_t rights, uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != THREAD_WAIT) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & THREAD_COMPLETION_RIGHT_WAIT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size || reply_capacity < sizeof(struct thread_result)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct thread_result), USER_BUFFER_WRITE)) {
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
  struct thread_result result = control->result;
  unlock_control(control);
  if (!copy_to_user(reply_address, &result, sizeof(result))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(result)};
}

void thread_request_execute(struct thread_request *request)
{
  struct process *process = request->process;
  KASSERT(process && request->caller);
  process_vm_require_mutation(process);
  if (request->operation == THREAD_SET_FS) {
    request->status = task_thread_bind_fs(request->caller,
        request->fs.identity, request->fs.fs_base);
    request->process = NULL;
    request->caller = NULL;
    return;
  }
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    /* Keep the drained process gate while admitting shared-kernel effects. */
    cpu_enable_interrupts();
    bool admitted = vm_kernel_owner_acquire(&owner);
    cpu_disable_interrupts();
    if (!admitted) {
      request->status = CALL_BUSY;
      request->process = NULL;
      request->caller = NULL;
      return;
    }
  }
  if (!process_vm_buffer_check(process, request->reply_address, sizeof(request->reply), true)) {
    request->status = CALL_BAD_BUFFER;
    goto done;
  }
  struct thread_control *control = thread_control_create();
  if (!control) {
    request->status = CALL_NO_MEMORY;
    goto done;
  }
  struct task *task;
  request->status = task_thread_prepare(request->caller, &request->input, control, &task);
  if (request->status != CALL_OK) {
    object_release(&control->object);
    goto done;
  }
  /* The task owns the first reference; the claimed observer gets its own. */
  KASSERT(object_retain(&control->object));
  struct capability_grant grant = {&control->object, THREAD_COMPLETION_RIGHT_WAIT, 0, 0};
  enum capability_result result = forwarder_derive_grant(&grant, task_forwarder_operation());
  if (result == CAP_OK) {
    result = capability_validate_grants(&process->capabilities, &grant, 1);
  }
  request->status = result == CAP_OK ? CALL_OK : result == CAP_NO_MEMORY ? CALL_NO_MEMORY :
      result == CAP_LIMIT ? CALL_LIMIT : CALL_DENIED;
  struct execution_group *group = process->execution_group;
  if (request->status == CALL_OK) {
    if (!execution_group_thread_begin(group)) {
      request->status = CALL_ENDPOINT_CLOSED;
    } else {
      spin_lock(&process->lifetime_lock);
      request->status = task_thread_attach_locked(task);
      if (request->status == CALL_OK) {
        capability_install_reserved(&request->reservation, &request->slot, &grant, 1,
            &request->reply.completion);
        grant = (struct capability_grant){0};
        /* VM admission keeps the new task out until the creator's final reply. */
        task_thread_enqueue(task);
      }
      spin_unlock(&process->lifetime_lock);
      execution_group_thread_end(group);
    }
  }
  if (request->status != CALL_OK) {
    capability_grant_release(&grant);
    task_thread_discard(task);
  }
done:
  vm_kernel_owner_release(&owner);
  request->process = NULL;
  request->caller = NULL;
}

struct syscall_result thread_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != THREAD_CREATE && operation != THREAD_EXIT && operation != THREAD_SET_FS &&
      operation != THREAD_ADDRESS_WAIT && operation != THREAD_ADDRESS_WAKE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & THREAD_RIGHT_MANAGE) &&
      (operation != THREAD_SET_FS || !(rights & THREAD_RIGHT_SET_FS))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct thread_message input = {0};
  if (request_size != sizeof(input.body)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&input.body, request_address, request_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  uint64_t words[sizeof(input.body) / sizeof(uint64_t)];
  memcpy(words, &input.body, sizeof(words));
  size_t used = operation == THREAD_EXIT ? 1 : operation == THREAD_SET_FS ? 2 :
      operation == THREAD_ADDRESS_WAIT ? 4 : operation == THREAD_ADDRESS_WAKE ? 3 : 5;
  for (size_t i = used; i < sizeof(words) / sizeof(words[0]); ++i) {
    if (words[i]) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }
  if (operation == THREAD_ADDRESS_WAIT) {
    return (struct syscall_result){address_wait(&input.body.address_wait), 0};
  }
  if (operation == THREAD_ADDRESS_WAKE) {
    if (reply_capacity < sizeof(struct thread_address_wake_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(struct thread_address_wake_reply), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    struct thread_address_wake_reply reply;
    enum call_status status = address_wake(&input.body.address_wake, &reply.woken);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (operation == THREAD_EXIT) {
    /* Dispatch releases its admitted object references before syscall leave. */
    task_thread_exit_request(input.body.exit_value);
    return (struct syscall_result){CALL_OK, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot = {0};
  struct process *process = process_current();
  if (operation == THREAD_CREATE) {
    if (reply_capacity < sizeof(struct thread_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(struct thread_reply), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    enum capability_result result = capability_request_reservation(1, &reservation, &slot);
    if (result != CAP_OK) {
      return (struct syscall_result){result == CAP_LIMIT ? CALL_LIMIT : CALL_NO_MEMORY, 0};
    }
  }
  struct thread_request *pending = (void *)bsp_request_prepare(BSP_SERVICE_THREAD);
  pending->process = process;
  pending->caller = task_current_user();
  pending->operation = operation;
  pending->input = input.body.create;
  pending->fs = input.body.set_fs;
  pending->reservation = reservation;
  pending->slot = slot;
  reservation = (struct capability_reservation){0};
  slot = (struct capability_reserved_slot){0};
  pending->reply_address = reply_address;
  pending->request.vm_process = process;
  pending->request.vm_mutator = pending->caller;
  bsp_request_submit_and_wait(&pending->request);
  struct syscall_result result = {pending->status, 0};
  struct thread_reply reply = pending->reply;
  reservation = pending->reservation;
  slot = pending->slot;
  pending->reservation = (struct capability_reservation){0};
  pending->slot = (struct capability_reserved_slot){0};
  bool reply_owned = pending->request.vm_reply_owned;
  bsp_request_release(&pending->request);
  if (result.status == CALL_OK && operation == THREAD_CREATE) {
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    result.reply_size = sizeof(reply);
  }
  capability_reservation_release(&reservation, &slot);
  if (reply_owned) {
    process_vm_reply_finish(process);
  }
  return result;
}
