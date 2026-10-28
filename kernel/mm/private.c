#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>
#include <kernel/task.h>

enum private_kind {
  PRIVATE_ORDINARY,
  PRIVATE_GUARDED,
  PRIVATE_SPARSE,
  PRIVATE_JIT,
};

struct private_allocation {
  struct private_allocation *next;
  uintptr_t address;
  size_t size;
  uint64_t identity; /* Zero for ordinary address/size allocations. */
  enum private_kind kind;
  size_t jit_writable_pages, jit_executable_pages;
  atomic_bool pinned; /* Pin acquisition under VM owner; retirement release is any-CPU safe. */
};

/* Globally unique even for concurrent private-space owners; zero is exhausted. */
static atomic_uint_fast64_t next_identity = 1;

static uint64_t reserve_identity(void)
{
  uint64_t value = atomic_load_explicit(&next_identity, memory_order_relaxed);
  while (value && !atomic_compare_exchange_weak_explicit(&next_identity, &value,
      value == UINT64_MAX ? 0 : value + 1, memory_order_relaxed, memory_order_relaxed)) {
  }
  return value;
}

/* Backs a bare reservation in bounded interrupts-off steps. The process VM
 * transaction stays exclusive across the yields between them. A size within
 * one step completes, or unwinds, in a single call and never yields, so the
 * request executor can run it inline. */
static enum mm_result back_private(struct process *process, uintptr_t base, size_t size)
{
  if (!private_memory_stepped(size)) {
    return vm_back(process->address_space, base, size, PAGE_USER | PAGE_WRITE);
  }
  struct vm_backing backing;
  enum mm_result result = vm_back_begin(process->address_space, base, size,
      PAGE_USER | PAGE_WRITE, &backing);
  if (result != MM_OK) {
    return result;
  }
  while (!vm_back_step(process->address_space, &backing, PRIVATE_MEMORY_STEP_PAGES)) {
    task_yield_masked();
    process_vm_require_mutation(process);
  }
  return backing.status;
}

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
  enum mm_result result = vm_reserve(process->address_space, size, PAGE_SIZE, address);
  if (result == MM_OK) {
    result = back_private(process, *address, size);
    if (result != MM_OK) {
      KASSERT(vm_release(process->address_space, *address, size) == MM_OK);
      *address = 0;
    }
  }
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
    if (allocation->kind == PRIVATE_ORDINARY && allocation->address == address &&
        allocation->size == size) {
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
  uint64_t identity = reserve_identity();
  if (!identity) {
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
  result = back_private(process, address + PAGE_SIZE, size);
  if (result != MM_OK) {
    KASSERT(result == MM_NO_MEMORY);
    KASSERT(vm_release(process->address_space, address, span) == MM_OK);
    kfree(allocation);
    return CALL_NO_MEMORY;
  }

  *allocation = (struct private_allocation){
    .next = process->allocations, .address = address, .size = span,
    .identity = identity, .kind = PRIVATE_GUARDED,
  };
  process->allocations = allocation;
  *reservation = (struct memory_reservation){allocation->identity, address, span};
  return CALL_OK;
}

enum call_status private_memory_reserve(struct process *process, size_t size,
    size_t alignment, struct memory_reservation *reservation)
{
  KASSERT(process && reservation);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  *reservation = (struct memory_reservation){0};
  uint64_t identity = reserve_identity();
  if (!identity) {
    return CALL_NO_MEMORY;
  }
  struct private_allocation *allocation = kmalloc(sizeof(*allocation));
  if (!allocation) {
    return CALL_NO_MEMORY;
  }
  uintptr_t address;
  enum mm_result result = vm_sparse_reserve(process->address_space, size, alignment, &address);
  if (result != MM_OK) {
    kfree(allocation);
    return result == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
  }
  *allocation = (struct private_allocation){
    .next = process->allocations, .address = address, .size = size,
    .identity = identity, .kind = PRIVATE_SPARSE,
  };
  process->allocations = allocation;
  *reservation = (struct memory_reservation){allocation->identity, address, size};
  return CALL_OK;
}

static enum call_status update_sparse(struct process *process,
    struct private_allocation *allocation, size_t offset, size_t size,
    enum vm_sparse_action action)
{
  struct vm_sparse_update *update;
  enum mm_result result = vm_sparse_begin(process->address_space, allocation->address,
      offset, size, action, &update);
  if (result != MM_OK) {
    return result == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
  }
  while (!vm_sparse_step(process->address_space, update, PRIVATE_MEMORY_STEP_PAGES)) {
    task_yield_masked();
    process_vm_require_mutation(process);
  }
  result = vm_sparse_finish(update);
  return result == MM_OK ? CALL_OK :
    result == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
}

enum call_status private_memory_update(struct process *process,
    const struct memory_subrange *range, bool commit)
{
  KASSERT(process && range);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  for (struct private_allocation *allocation = process->allocations; allocation;
      allocation = allocation->next) {
    if (!range->identity || allocation->identity != range->identity) {
      continue;
    }
    if (atomic_load_explicit(&allocation->pinned, memory_order_acquire)) {
      return CALL_BUSY;
    }
    if (allocation->kind != PRIVATE_SPARSE || !range->size ||
        (range->offset & (PAGE_SIZE - 1)) || (range->size & (PAGE_SIZE - 1)) ||
        range->offset > allocation->size || range->size > allocation->size - range->offset) {
      return CALL_BAD_REQUEST;
    }
    return update_sparse(process, allocation, range->offset, range->size,
        commit ? VM_SPARSE_COMMIT : VM_SPARSE_DECOMMIT);
  }
  return CALL_BAD_REQUEST;
}

bool private_memory_release_stepped(struct process *process, uint64_t identity)
{
  KASSERT(process);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  for (struct private_allocation *allocation = process->allocations; allocation;
       allocation = allocation->next) {
    if (allocation->identity == identity) {
      return allocation->kind == PRIVATE_SPARSE;
    }
  }
  return false;
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
      if (allocation->kind != PRIVATE_GUARDED && allocation->kind != PRIVATE_SPARSE) {
        return CALL_BAD_REQUEST;
      }
      if (atomic_load_explicit(&allocation->pinned, memory_order_acquire)) {
        return CALL_BUSY;
      }
      if (allocation->kind == PRIVATE_SPARSE) {
        enum call_status status = update_sparse(process, allocation, 0, allocation->size,
            VM_SPARSE_RELEASE);
        if (status != CALL_OK) {
          return status;
        }
      } else {
        KASSERT(vm_free(process->address_space, allocation->address, allocation->size) == MM_OK);
      }
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
    if (atomic_load_explicit(&allocation->pinned, memory_order_acquire)) {
      return CALL_BUSY;
    }
    if (allocation->kind != PRIVATE_GUARDED) {
      return CALL_BAD_REQUEST;
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
    atomic_store_explicit(&allocation->pinned, true, memory_order_relaxed);
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
  KASSERT(context);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (context->allocation) {
    KASSERT(atomic_load_explicit(&context->allocation->pinned, memory_order_relaxed));
    /* Final record access: an AP may release/free it immediately after this
     * store. Scheduler retirement already left the stack/TLS and private root. */
    atomic_store_explicit(&context->allocation->pinned, false, memory_order_release);
  }
  *context = (struct private_context){0};
}

/* Initial per-process policy, not ABI. All JIT states spend the same allowance. */
#define JIT_BACKING_LIMIT (UINT64_C(256) * 1024 * 1024)
#define JIT_REGION_LIMIT 4096

static enum call_status jit_mm_status(enum mm_result status)
{
  return status == MM_OK ? CALL_OK : status == MM_NO_MEMORY ? CALL_NO_MEMORY :
      status == MM_BUSY ? CALL_BUSY : CALL_BAD_REQUEST;
}

enum call_status private_jit_allocate(struct process *process, size_t size,
    struct jit_region *region)
{
  KASSERT(process && region && size && !(size & (PAGE_SIZE - 1)));
  process_vm_require_mutation(process);
  *region = (struct jit_region){0};
  KASSERT(process->jit_bytes <= JIT_BACKING_LIMIT);
  if (process->jit_regions == JIT_REGION_LIMIT || size > JIT_BACKING_LIMIT - process->jit_bytes) {
    return CALL_LIMIT;
  }
  uint64_t identity = reserve_identity();
  if (!identity) {
    return CALL_NO_MEMORY;
  }
  struct private_allocation *allocation = kmalloc(sizeof(*allocation));
  if (!allocation) {
    return CALL_NO_MEMORY;
  }
  /* The exclusive process token reserves quota through preparation/unwind. */
  process->jit_bytes += size;
  ++process->jit_regions;
  uintptr_t address = 0;
  enum mm_result result = vm_reserve(process->address_space, size, PAGE_SIZE, &address);
  if (result == MM_OK) {
    result = back_private(process, address, size);
    if (result != MM_OK) {
      KASSERT(vm_release(process->address_space, address, size) == MM_OK);
    }
  }
  if (result != MM_OK) {
    process->jit_bytes -= size;
    --process->jit_regions;
    kfree(allocation);
    return jit_mm_status(result);
  }
  *allocation = (struct private_allocation){.next = process->allocations,
    .address = address, .size = size, .identity = identity, .kind = PRIVATE_JIT,
    .jit_writable_pages = size / PAGE_SIZE};
  process->allocations = allocation;
  process->jit_writable_bytes += size;
  *region = (struct jit_region){identity, address, size};
  return CALL_OK;
}

enum call_status private_jit_protect(struct process *process, uint64_t identity,
    size_t offset, size_t size, enum jit_state state)
{
  process_vm_require_mutation(process);
  if (!identity || !size || (offset & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1)) ||
      (state != JIT_WRITABLE && state != JIT_READ_ONLY && state != JIT_EXECUTABLE)) {
    return CALL_BAD_REQUEST;
  }
  struct private_allocation *allocation = process->allocations;
  while (allocation && (allocation->kind != PRIVATE_JIT || allocation->identity != identity)) {
    allocation = allocation->next;
  }
  if (!allocation || offset > allocation->size || size > allocation->size - offset) {
    return CALL_BAD_REQUEST;
  }
  unsigned permissions = PAGE_USER | (state == JIT_WRITABLE ? PAGE_WRITE :
      state == JIT_EXECUTABLE ? PAGE_EXEC : 0);
  struct vm_protection protection;
  enum mm_result result = vm_protect_begin(process->address_space,
      allocation->address + offset, size, permissions, &protection);
  if (result != MM_OK) {
    return jit_mm_status(result);
  }
  while (!vm_protect_step(&protection, PRIVATE_MEMORY_STEP_PAGES)) {
    task_yield_masked();
    process_vm_require_mutation(process);
  }
  if (protection.status != MM_OK) {
    return jit_mm_status(protection.status);
  }
  size_t writable = protection.old_writable_pages;
  size_t executable = protection.old_executable_pages;
  KASSERT(allocation->jit_writable_pages >= writable &&
      allocation->jit_executable_pages >= executable);
  allocation->jit_writable_pages -= writable;
  allocation->jit_executable_pages -= executable;
  process->jit_writable_bytes -= writable * PAGE_SIZE;
  process->jit_executable_bytes -= executable * PAGE_SIZE;
  if (state == JIT_WRITABLE) {
    allocation->jit_writable_pages += size / PAGE_SIZE;
    process->jit_writable_bytes += size;
  } else if (state == JIT_EXECUTABLE) {
    allocation->jit_executable_pages += size / PAGE_SIZE;
    process->jit_executable_bytes += size;
  }
  return CALL_OK;
}

enum call_status private_jit_release(struct process *process, uint64_t identity, size_t *bytes)
{
  process_vm_require_mutation(process);
  *bytes = 0;
  if (!identity) {
    return CALL_BAD_REQUEST;
  }
  struct private_allocation **link = &process->allocations;
  while (*link && ((*link)->kind != PRIVATE_JIT || (*link)->identity != identity)) {
    link = &(*link)->next;
  }
  struct private_allocation *allocation = *link;
  if (!allocation) {
    return CALL_BAD_REQUEST;
  }
  *bytes = allocation->size;
  struct vm_reclaim reclaim;
  KASSERT(vm_free_begin(process->address_space, allocation->address, allocation->size,
      &reclaim) == MM_OK);
  while (!vm_free_step(&reclaim, PRIVATE_MEMORY_STEP_PAGES)) {
    task_yield_masked();
    process_vm_require_mutation(process);
  }
  /* Only actual reclamation returns quota; a stop cannot skip this boundary. */
  process->jit_bytes -= allocation->size;
  --process->jit_regions;
  process->jit_writable_bytes -= allocation->jit_writable_pages * PAGE_SIZE;
  process->jit_executable_bytes -= allocation->jit_executable_pages * PAGE_SIZE;
  *link = allocation->next;
  kfree(allocation);
  return CALL_OK;
}

void private_memory_discard_records(struct process *process)
{
  KASSERT(!cpu_interrupts_enabled() && process);
  size_t released = 0;
  while (process->allocations) {
    struct private_allocation *allocation = process->allocations;
    KASSERT(!atomic_load_explicit(&allocation->pinned, memory_order_acquire));
    process->allocations = allocation->next;
    kfree(allocation);
    if (!(++released % 256) && task_current_actor()) {
      task_yield_masked();
    }
  }
}
