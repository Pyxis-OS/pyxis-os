#include <kernel/object/clipboard.h>
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
  atomic_init(&process->result_set, false);
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
  KASSERT(process->lifetime != PROCESS_SUBMITTED && !process->task &&
      !process->task_storage);

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
  if (!process || process->lifetime != PROCESS_PREPARING || process->task_storage) {
    return MM_INVALID;
  }
  return destroy_process(process);
}

bool process_task_available(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  return process->lifetime == PROCESS_PREPARING && !process->task_storage;
}

void process_task_attach(struct process *process, struct task *task)
{
  KASSERT(arch_cpu_index() == 0 && task && process_task_available(process));
  spin_lock(&process->lifetime_lock);
  process->task = task;
  process->task_storage = true;
  spin_unlock(&process->lifetime_lock);
}

void process_task_discard(struct process *process, struct task *task)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_PREPARING && process->task == task &&
      process->task_storage);
  process->task = NULL;
  process->task_storage = false;
  spin_unlock(&process->lifetime_lock);
}

void process_publish(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_PREPARING && process->task &&
      process->task_storage);
  process->lifetime = PROCESS_SUBMITTED;
  spin_unlock(&process->lifetime_lock);
}

void process_request_stop(struct process *process)
{
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime != PROCESS_PREPARING);
  if (!process->stopping &&
      !atomic_load_explicit(&process->result_set, memory_order_acquire)) {
    process->stopping = true;
    if (process->task) {
      task_request_stop(process->task);
    }
  }
  spin_unlock(&process->lifetime_lock);
}

void process_set_result(struct process *process, struct process_result result)
{
  KASSERT(process->lifetime == PROCESS_SUBMITTED &&
      !atomic_load_explicit(&process->result_set, memory_order_relaxed));
  KASSERT(result.kind == PROCESS_EXITED || result.kind == PROCESS_FAULTED ||
      result.kind == PROCESS_TERMINATED);
  process->result = result;
  atomic_store_explicit(&process->result_set, true, memory_order_release);
}

void process_task_detach(struct process *process, struct task *task)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_SUBMITTED && process->task == task &&
      process->task_storage &&
      atomic_load_explicit(&process->result_set, memory_order_acquire));
  process->task = NULL;
  spin_unlock(&process->lifetime_lock);
}

void process_task_reclaimed(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_SUBMITTED && !process->task &&
      process->task_storage);
  process->task_storage = false;
  process->lifetime = PROCESS_RETIRED;
  spin_unlock(&process->lifetime_lock);

  struct process_control *control = process->control;
  struct execution_group *group = process->execution_group;
  struct process_result result = process->result;
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
}
