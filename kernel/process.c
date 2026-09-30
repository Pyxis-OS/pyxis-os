#include <kernel/object/endpoint.h>
#include <kernel/object/keyboard.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/object/process.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/display.h>
#include <kernel/process.h>

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
  *process = (struct process){.space = space, .address_space = address_space};
  process->control = process_control_create();
  if (!process->control) {
    kfree(process);
    return MM_NO_MEMORY;
  }
  *result = process;
  return MM_OK;
}

enum mm_result process_destroy(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  if (!process) {
    return MM_INVALID;
  }

  keyboard_process_exit(process);
  display_process_exit(process);
  enum mm_result result = vm_space_destroy(process->address_space);
  if (result != MM_OK) {
    return result;
  }
  endpoint_process_exit(process);
  private_memory_discard_records(process);
  capability_table_destroy(&process->capabilities);
  if (process->control) {
    object_release(&process->control->object);
  }
  if (process->execution_group) {
    /* Unpublished preparation. The reaper takes published membership first. */
    object_release(&process->execution_group->object);
  }
  kfree(process);
  return MM_OK;
}
