#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>

struct private_allocation {
  struct private_allocation *next;
  uintptr_t address;
  size_t size;
};

enum mm_result private_memory_allocate(struct process *process, size_t size,
                                        uintptr_t *address)
{
  KASSERT(process && process == process_current() && address);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(size && !(size & (PAGE_SIZE - 1)));
  *address = 0;

  struct private_allocation *allocation = kmalloc(sizeof(*allocation));
  if (!allocation) {
    return MM_NO_MEMORY;
  }
  enum mm_result result = vm_alloc(process->address_space, size, PAGE_SIZE,
      PAGE_USER | PAGE_WRITE, address);
  if (result != MM_OK) {
    kfree(allocation);
    return result;
  }

  *allocation = (struct private_allocation){process->allocations, *address, size};
  process->allocations = allocation;
  return MM_OK;
}

enum mm_result private_memory_release(struct process *process, uintptr_t address,
                                       size_t size)
{
  KASSERT(process && process == process_current());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct private_allocation **link = &process->allocations;
  while (*link) {
    struct private_allocation *allocation = *link;
    if (allocation->address == address && allocation->size == size) {
      /* Matching a service-owned record is required before touching VM. */
      KASSERT(vm_free(process->address_space, address, size) == MM_OK);
      *link = allocation->next;
      kfree(allocation);
      return MM_OK;
    }
    link = &allocation->next;
  }
  return MM_INVALID;
}

void private_memory_discard_records(struct process *process)
{
  KASSERT(arch_cpu_index() == 0 && process);
  while (process->allocations) {
    struct private_allocation *allocation = process->allocations;
    process->allocations = allocation->next;
    kfree(allocation);
  }
}
