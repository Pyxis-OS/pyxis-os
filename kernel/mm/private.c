#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>

struct private_allocation {
  struct private_allocation *next;
  uintptr_t address;
  size_t size;
  uint64_t identity; /* Zero for ordinary address/size allocations. */
  bool pinned;
};

/* BSP-owned and never reused, including across processes and address reuse. */
static uint64_t next_identity = 1;

enum mm_result private_memory_allocate(struct process *process, size_t size,
                                        uintptr_t *address)
{
  KASSERT(process && address);
  process_vm_require_mutation(process);
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

  *allocation = (struct private_allocation){
    .next = process->allocations, .address = *address, .size = size,
  };
  process->allocations = allocation;
  return MM_OK;
}

enum mm_result private_memory_release(struct process *process, uintptr_t address,
                                       size_t size)
{
  KASSERT(process);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct private_allocation **link = &process->allocations;
  while (*link) {
    struct private_allocation *allocation = *link;
    if (!allocation->identity && allocation->address == address && allocation->size == size) {
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

enum call_status private_memory_allocate_guarded(struct process *process, size_t size,
    struct memory_reservation *reservation)
{
  KASSERT(process && reservation);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(size && !(size & (PAGE_SIZE - 1)) && size <= SIZE_MAX - PAGE_SIZE);
  *reservation = (struct memory_reservation){0};
  if (!next_identity) {
    return CALL_NO_MEMORY;
  }

  struct private_allocation *allocation = kmalloc(sizeof(*allocation));
  if (!allocation) {
    return CALL_NO_MEMORY;
  }
  size_t span = size + PAGE_SIZE;
  uintptr_t address;
  enum mm_result result = vm_reserve(process->address_space, span, PAGE_SIZE, &address);
  if (result != MM_OK) {
    KASSERT(result == MM_NO_MEMORY);
    kfree(allocation);
    return CALL_NO_MEMORY;
  }
  result = vm_back(process->address_space, address + PAGE_SIZE, size,
      PAGE_USER | PAGE_WRITE);
  if (result != MM_OK) {
    KASSERT(result == MM_NO_MEMORY);
    KASSERT(vm_release(process->address_space, address, span) == MM_OK);
    kfree(allocation);
    return CALL_NO_MEMORY;
  }

  *allocation = (struct private_allocation){
    .next = process->allocations, .address = address, .size = span,
    .identity = next_identity++,
  };
  process->allocations = allocation;
  *reservation = (struct memory_reservation){allocation->identity, address, span};
  return CALL_OK;
}

enum call_status private_memory_release_reservation(struct process *process,
    uint64_t identity)
{
  KASSERT(process);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!identity) {
    return CALL_BAD_REQUEST;
  }
  struct private_allocation **link = &process->allocations;
  while (*link) {
    struct private_allocation *allocation = *link;
    if (allocation->identity == identity) {
      if (allocation->pinned) {
        return CALL_BUSY;
      }
      KASSERT(vm_free(process->address_space, allocation->address, allocation->size) == MM_OK);
      *link = allocation->next;
      kfree(allocation);
      return CALL_OK;
    }
    link = &allocation->next;
  }
  return CALL_BAD_REQUEST;
}

enum call_status private_context_pin(struct process *process, uint64_t identity,
    struct private_context *context)
{
  KASSERT(process && context && !context->allocation);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  *context = (struct private_context){0};
  if (!identity) {
    return CALL_BAD_REQUEST;
  }
  for (struct private_allocation *allocation = process->allocations; allocation;
      allocation = allocation->next) {
    if (allocation->identity != identity) {
      continue;
    }
    if (allocation->pinned) {
      return CALL_BUSY;
    }
    struct page_translation translation;
    if (vm_query(process->address_space, allocation->address, &translation) != MM_NOT_MAPPED) {
      return CALL_BAD_REQUEST;
    }
    for (size_t offset = PAGE_SIZE; offset < allocation->size; offset += PAGE_SIZE) {
      if (vm_query(process->address_space, allocation->address + offset, &translation) != MM_OK ||
          (translation.permissions & (PAGE_USER | PAGE_WRITE | PAGE_EXEC)) !=
              (PAGE_USER | PAGE_WRITE)) {
        return CALL_BAD_REQUEST;
      }
    }
    allocation->pinned = true;
    *context = (struct private_context){
      .allocation = allocation, .identity = identity,
      .address = allocation->address, .size = allocation->size,
      .usable_address = allocation->address + PAGE_SIZE,
      .usable_size = allocation->size - PAGE_SIZE,
    };
    return CALL_OK;
  }
  return CALL_BAD_REQUEST;
}

void private_context_unpin(struct private_context *context)
{
  KASSERT(arch_cpu_index() == 0 && context);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (context->allocation) {
    KASSERT(context->allocation->pinned);
    context->allocation->pinned = false;
  }
  *context = (struct private_context){0};
}

void private_memory_discard_records(struct process *process)
{
  KASSERT(arch_cpu_index() == 0 && process);
  while (process->allocations) {
    struct private_allocation *allocation = process->allocations;
    KASSERT(!allocation->pinned);
    process->allocations = allocation->next;
    kfree(allocation);
  }
}
