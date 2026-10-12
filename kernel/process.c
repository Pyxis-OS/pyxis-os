#include <kernel/object/clipboard.h>
#include <kernel/completion.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/pointer.h>
#include <kernel/object/audio.h>
#include <kernel/object/bluetooth_hci.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/object/process.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/display.h>
#include <kernel/process.h>
#include <kernel/task.h>

/* Tunable storage policy, including the initial and retiring threads. */
#define PROCESS_THREAD_LIMIT 64
#define PROCESS_RESULT_KIND_SHIFT 32
#define PROCESS_RESULT_KIND_MASK UINT64_C(0xffffffff00000000)
#define PROCESS_RESULT_STATUS_MASK UINT64_C(0x00000000ffffffff)

_Static_assert(__atomic_always_lock_free(sizeof(atomic_uint_fast64_t), 0),
               "process result election must be fault-safe");

enum mm_result process_create(struct space *space, struct vm_space *address_space,
                              struct process **result)
{
  KASSERT(arch_cpu_index() == 0);
  if (result) {
    *result = NULL;
  }
  if (!result || !space || !address_space || address_space == vm_kernel_space()) {
    return MM_INVALID;
  }

  struct process *process = kmalloc(sizeof(*process));
  if (!process) {
    return MM_NO_MEMORY;
  }
  *process = (struct process){
    .space = space, .address_space = address_space, .lifetime = PROCESS_PREPARING,
  };
  atomic_init(&process->lifetime_lock.locked, false);
  atomic_init(&process->vm.lock.locked, false);
  atomic_init(&process->vm.phase, PROCESS_VM_OPEN);
  atomic_init(&process->terminal_result, 0);
  atomic_init(&process->completion_gate, NULL);
  process->group_member.process = process;
  process->control = process_control_create();
  if (!process->control) {
    kfree(process);
    return MM_NO_MEMORY;
  }
  process_control_attach_process(process->control, process);
  *result = process;
  return MM_OK;
}

static enum mm_result destroy_process(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(process->lifetime != PROCESS_SUBMITTED && !process->tasks &&
      !process->task_count && !process->task_storage_count);
  process_vm_retire(process);

  clipboard_process_exit(process);
  keyboard_process_exit(process);
  pointer_process_exit(process);
  bluetooth_hci_process_exit(process);
  display_process_exit(process);
  audio_process_exit(process);
  enum mm_result result = vm_space_destroy(process->address_space);
  if (result != MM_OK) {
    return result;
  }
  endpoint_process_exit(process);
  native_completion_process_exit(process);
  private_memory_discard_records(process);
  capability_table_destroy(&process->capabilities);
  if (process->control) {
    process_control_detach_process(process->control);
    object_release(&process->control->object);
  }
  if (process->execution_group) {
    /* Unpublished preparation. The reaper takes published membership first. */
    object_release(&process->execution_group->object);
  }
  kfree(process);
  return MM_OK;
}

enum mm_result process_destroy(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  if (!process || process->lifetime != PROCESS_PREPARING || process->task_storage_count) {
    return MM_INVALID;
  }
  return destroy_process(process);
}

bool process_task_available(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  return process->lifetime == PROCESS_PREPARING && !process->task_count &&
    !process->task_storage_count;
}

static void attach_task_locked(struct process *process,
    struct process_task_member *member, struct task *task)
{
  KASSERT(member && task && !member->next && !member->task);
  KASSERT(process->task_storage_count < PROCESS_THREAD_LIMIT);
  member->task = task;
  member->next = process->tasks;
  process->tasks = member;
  ++process->task_count;
  ++process->task_storage_count;
}

void process_task_attach(struct process *process, struct process_task_member *member,
                         struct task *task)
{
  KASSERT(arch_cpu_index() == 0 && task && process_task_available(process));
  spin_lock(&process->lifetime_lock);
  attach_task_locked(process, member, task);
  spin_unlock(&process->lifetime_lock);
}

void process_task_discard(struct process *process, struct process_task_member *member)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_PREPARING && process->tasks == member &&
      process->task_count == 1 && process->task_storage_count == 1);
  process->tasks = NULL;
  process->task_count = 0;
  process->task_storage_count = 0;
  member->next = NULL;
  member->task = NULL;
  spin_unlock(&process->lifetime_lock);
}

void process_publish(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_PREPARING && process->tasks &&
      process->task_count == 1 && process->task_storage_count == 1);
  process->lifetime = PROCESS_SUBMITTED;
  spin_unlock(&process->lifetime_lock);
}

enum call_status process_thread_attach_locked(struct process *process,
    struct process_task_member *member, struct task *task)
{
  KASSERT(arch_cpu_index() == 0);
  if (process->lifetime != PROCESS_SUBMITTED || process->stopping ||
      process_result_committed(process)) {
    return CALL_ENDPOINT_CLOSED;
  }
  if (process->task_storage_count >= PROCESS_THREAD_LIMIT) {
    return CALL_LIMIT;
  }
  attach_task_locked(process, member, task);
  return CALL_OK;
}

void process_request_stop(struct process *process)
{
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime != PROCESS_PREPARING);
  if (!process->stopping) {
    process->stopping = true;
    process_set_result(process, (struct process_result){.kind = PROCESS_TERMINATED});
    for (struct process_task_member *member = process->tasks;
         member; member = member->next) {
      task_request_stop(member->task);
    }
  }
  spin_unlock(&process->lifetime_lock);
}

void process_set_result(struct process *process, struct process_result result)
{
  KASSERT(process->lifetime == PROCESS_SUBMITTED);
  KASSERT(result.kind == PROCESS_EXITED || result.kind == PROCESS_FAULTED ||
      result.kind == PROCESS_TERMINATED);
  KASSERT(result.exit_status == (int32_t)result.exit_status &&
      (result.kind == PROCESS_EXITED || !result.exit_status));
  uint_fast64_t elected = result.kind << PROCESS_RESULT_KIND_SHIFT |
    ((uint_fast64_t)(uint32_t)result.exit_status & PROCESS_RESULT_STATUS_MASK);
  uint_fast64_t expected = 0;
  atomic_compare_exchange_strong_explicit(&process->terminal_result, &expected,
      elected, memory_order_release, memory_order_relaxed);
}

bool process_result_committed(struct process *process)
{
  return atomic_load_explicit(&process->terminal_result, memory_order_acquire) != 0;
}

bool process_result_get(struct process *process, struct process_result *result)
{
  KASSERT(result);
  uint_fast64_t elected = atomic_load_explicit(&process->terminal_result,
      memory_order_acquire);
  *result = (struct process_result){
    .kind = (elected & PROCESS_RESULT_KIND_MASK) >> PROCESS_RESULT_KIND_SHIFT,
    .exit_status = (int32_t)(elected & PROCESS_RESULT_STATUS_MASK),
  };
  return elected != 0;
}

void process_task_detach(struct process *process, struct process_task_member *member)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_SUBMITTED && process->task_count &&
      process->task_storage_count >= process->task_count);
  struct process_task_member **link = &process->tasks;
  while (*link != member) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = member->next;
  member->next = NULL;
  member->task = NULL;
  --process->task_count;
  bool last = !process->task_count;
  if (last) {
    process->stopping = true;
    if (!process_result_committed(process)) {
      process_set_result(process, (struct process_result){.kind = PROCESS_EXITED});
    }
  }
  spin_unlock(&process->lifetime_lock);
  if (last) {
    /* No remaining task can hold a root, continuation or pending request. */
    process_vm_retire(process);
  }
}

bool process_task_reclaimed(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_SUBMITTED &&
      process->task_storage_count > process->task_count);
  --process->task_storage_count;
  if (process->task_storage_count) {
    spin_unlock(&process->lifetime_lock);
    return false;
  }
  KASSERT(!process->tasks && !process->task_count && process_result_committed(process));
  process->lifetime = PROCESS_RETIRED;
  spin_unlock(&process->lifetime_lock);

  struct process_control *control = process->control;
  struct execution_group *group = process->execution_group;
  struct process_result result;
  KASSERT(process_result_get(process, &result));
  process_control_detach_process(control);
  process->control = NULL;
  if (group) {
    execution_group_member_detach(group, &process->group_member);
    process->execution_group = NULL;
  }

  struct execution_group *previous = object_cleanup_enter(group);
  KASSERT(destroy_process(process) == MM_OK);
  process_control_complete(control, result);
  object_release(&control->object);
  object_cleanup_leave(previous);
  if (group) {
    execution_group_member_complete(group);
  }
  return true;
}
