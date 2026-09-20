#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
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
  *result = process;
  return MM_OK;
}

enum mm_result process_destroy(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  if (!process) {
    return MM_INVALID;
  }

  enum mm_result result = vm_space_destroy(process->address_space);
  if (result != MM_OK) {
    return result;
  }
  kfree(process);
  return MM_OK;
}
