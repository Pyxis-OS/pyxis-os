#include <arch/smp.h>
#include <abi/startup.h>
#include <kernel/memory.h>
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

enum mm_result process_prepare_startup(struct process *process, handle_t output,
                                       handle_t content)
{
  KASSERT(arch_cpu_index() == 0);
  if (!process || process->startup_address) {
    return MM_INVALID;
  }

  uintptr_t address;
  enum mm_result result = vm_alloc(process->address_space, PAGE_SIZE, PAGE_SIZE,
                                    PAGE_USER, &address);
  if (result != MM_OK) {
    return result;
  }

  uintptr_t scratch;
  result = vm_reserve(vm_kernel_space(), PAGE_SIZE, PAGE_SIZE, &scratch);
  if (result == MM_OK) {
    struct page_translation translation;
    KASSERT(vm_query(process->address_space, address, &translation) == MM_OK);
    result = vm_map(vm_kernel_space(), scratch, translation.physical, PAGE_WRITE);
    if (result == MM_OK) {
      const struct startup_info startup = {
        .version = STARTUP_VERSION,
        .size = sizeof(struct startup_info),
        .output = output,
        .content = content,
      };
      /* Fill an inactive user page through a borrowed kernel alias. Its user
       * mapping is never writable, and VM zeroed the rest of the page. */
      memcpy((void *)scratch, &startup, sizeof(startup));
      phys_addr_t physical;
      KASSERT(vm_unmap(vm_kernel_space(), scratch, &physical) == MM_OK);
      KASSERT(physical == translation.physical);
    }
    KASSERT(vm_release(vm_kernel_space(), scratch, PAGE_SIZE) == MM_OK);
  }

  if (result != MM_OK) {
    KASSERT(vm_free(process->address_space, address, PAGE_SIZE) == MM_OK);
    return result;
  }
  process->startup_address = address;
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
  capability_table_destroy(&process->capabilities);
  kfree(process);
  return MM_OK;
}
