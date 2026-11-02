#include <arch/cpu.h>
#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/image.h>
#include <kernel/limit.h>
#include <kernel/memory.h>
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
  PRIVATE_IMAGE,
};

struct private_image_range {
  uintptr_t address;
  size_t size;
};

struct private_allocation {
  struct private_allocation *next;
  uintptr_t address;
  size_t size;
  uint64_t identity; /* Zero for ordinary address/size allocations. */
  enum private_kind kind;
  size_t jit_writable_pages, jit_executable_pages;
  /* PRIVATE_IMAGE: its RELRO segments, sealed at most once. */
  struct private_image_range *image_relro;
  size_t image_relro_count;
  bool image_sealed;
  atomic_bool pinned; /* Pin acquisition under VM owner; retirement release is any-CPU safe. */
};

/* Globally unique even for concurrent private-space owners; zero is exhausted. */
static atomic_uint_fast64_t next_identity = 1;

uint64_t private_memory_identity(void)
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

enum call_status private_memory_allocate(struct process *process, size_t size,
    uintptr_t *address, struct call_limit *detail)
{
  KASSERT(process && address);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(size && !(size & (PAGE_SIZE - 1)));
  *address = 0;
  if (size > arch_user_vm_size()) {
    return call_limit_set(detail, CALL_LIMIT_MEMORY_ADDRESS, size, arch_user_vm_size(),
        CALL_LIMIT_HAS_REQUESTED | CALL_LIMIT_HAS_LIMIT);
  }

  struct private_allocation *allocation = kmalloc(sizeof(*allocation));
  if (!allocation) {
    return CALL_NO_MEMORY;
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
    KASSERT(result == MM_NO_MEMORY);
    return CALL_NO_MEMORY;
  }

  *allocation = (struct private_allocation){
    .next = process->allocations, .address = *address, .size = size,
  };
  process->allocations = allocation;
  return CALL_OK;
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
    struct memory_reservation *reservation, struct call_limit *detail)
{
  KASSERT(process && reservation);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(size && !(size & (PAGE_SIZE - 1)) && size <= SIZE_MAX - PAGE_SIZE);
  *reservation = (struct memory_reservation){0};
  size_t capacity = arch_user_vm_size() - PAGE_SIZE;
  if (size > capacity) {
    return call_limit_set(detail, CALL_LIMIT_MEMORY_ADDRESS, size, capacity,
        CALL_LIMIT_HAS_REQUESTED | CALL_LIMIT_HAS_LIMIT);
  }
  uint64_t identity = private_memory_identity();
  if (!identity) {
    return call_limit_set(detail, CALL_LIMIT_MEMORY_IDENTITY, UINT64_MAX, UINT64_MAX,
        CALL_LIMIT_HAS_REQUESTED | CALL_LIMIT_HAS_LIMIT | CALL_LIMIT_REQUESTED_SATURATED);
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
    size_t alignment, struct memory_reservation *reservation, struct call_limit *detail)
{
  KASSERT(process && reservation);
  process_vm_require_mutation(process);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  *reservation = (struct memory_reservation){0};
  if (size > arch_user_vm_size()) {
    return call_limit_set(detail, CALL_LIMIT_MEMORY_ADDRESS, size, arch_user_vm_size(),
        CALL_LIMIT_HAS_REQUESTED | CALL_LIMIT_HAS_LIMIT);
  }
  uint64_t identity = private_memory_identity();
  if (!identity) {
    return call_limit_set(detail, CALL_LIMIT_MEMORY_IDENTITY, UINT64_MAX, UINT64_MAX,
        CALL_LIMIT_HAS_REQUESTED | CALL_LIMIT_HAS_LIMIT | CALL_LIMIT_REQUESTED_SATURATED);
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
  uint64_t identity = private_memory_identity();
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
    kfree(allocation->image_relro);
    kfree(allocation);
    if (!(++released % 256) && task_current_actor()) {
      task_yield_masked();
    }
  }
}

/* Changes a range of one eager backing to final permissions in bounded steps,
 * keeping the process transaction across yields. */
static enum mm_result protect_range(struct process *process, uintptr_t base, size_t size,
    unsigned permissions)
{
  struct vm_protection protection;
  enum mm_result result = vm_protect_begin(process->address_space, base, size, permissions,
      &protection);
  if (result != MM_OK) {
    return result;
  }
  while (!vm_protect_step(&protection, PRIVATE_MEMORY_STEP_PAGES)) {
    task_yield_masked();
    process_vm_require_mutation(process);
  }
  return protection.status;
}

/* Copies file bytes into freshly backed, still writable, non-executable pages
 * through the kernel's frame alias. The process's threads are quiesced. */
static void copy_segment(struct process *process, uintptr_t base, const uint8_t *bytes,
    size_t size)
{
  for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
    struct page_translation translation;
    KASSERT(vm_query(process->address_space, base + offset, &translation) == MM_OK);
    size_t count = size - offset < PAGE_SIZE ? size - offset : PAGE_SIZE;
    void *page = arch_frame_map(translation.physical);
    memcpy(page, bytes + offset, count);
    arch_frame_unmap();
    if (!((offset / PAGE_SIZE + 1) % PRIVATE_MEMORY_STEP_PAGES)) {
      task_yield_masked();
      process_vm_require_mutation(process);
    }
  }
}

struct image_reservation {
  uintptr_t address;
  size_t size;
  bool backed;
};

static void unwind_image(struct process *process, struct image_reservation *reserved,
    size_t count)
{
  while (count) {
    struct image_reservation *range = &reserved[--count];
    if (!range->backed) {
      KASSERT(vm_release(process->address_space, range->address, range->size) == MM_OK);
      continue;
    }
    struct vm_reclaim reclaim;
    KASSERT(vm_free_begin(process->address_space, range->address, range->size,
        &reclaim) == MM_OK);
    while (!vm_free_step(&reclaim, PRIVATE_MEMORY_STEP_PAGES)) {
      task_yield_masked();
      process_vm_require_mutation(process);
    }
  }
}

static size_t page_round(uint64_t size)
{
  return (size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
}

enum call_status private_image_map(struct process *process, const void *bytes,
    const struct image_dynamic *image, struct memory_image *result)
{
  KASSERT(process && bytes && image && result);
  process_vm_require_mutation(process);
  *result = (struct memory_image){0};
  size_t relro_count = 0;
  for (size_t i = 0; i < image->segment_count; ++i) {
    if (image_dynamic_segment(bytes, i).flags & P1D_RELRO) {
      ++relro_count;
    }
  }
  uint64_t identity = private_memory_identity();
  if (!identity) {
    return CALL_NO_MEMORY;
  }
  /* A library also reserves the gaps between its segments. */
  size_t reserved_capacity = image->segment_count * (image->library ? 2 : 1);
  struct private_allocation *allocation = kmalloc(sizeof(*allocation));
  struct private_image_range *relro = relro_count ?
      kmalloc(relro_count * sizeof(*relro)) : NULL;
  struct image_reservation *reserved = kmalloc(reserved_capacity * sizeof(*reserved));
  if (!allocation || (relro_count && !relro) || !reserved) {
    kfree(allocation);
    kfree(relro);
    kfree(reserved);
    return CALL_NO_MEMORY;
  }

  struct vm_space *space = process->address_space;
  uintptr_t base = 0;
  enum mm_result status = MM_OK;
  if (image->library) {
    /* Find room for the whole span, then reserve it piece by piece. The
     * process transaction keeps every other mapping change out meanwhile. */
    status = vm_reserve(space, image->span, PAGE_SIZE, &base);
    if (status == MM_OK) {
      KASSERT(vm_release(space, base, image->span) == MM_OK);
    }
  }

  size_t reserved_count = 0;
  size_t relro_index = 0;
  size_t payload = image->payload;
  uintptr_t previous_end = base;
  uintptr_t first = 0;
  for (size_t i = 0; status == MM_OK && i < image->segment_count; ++i) {
    struct p1f_segment segment = image_dynamic_segment(bytes, i);
    uintptr_t address = base + segment.virtual_address;
    size_t size = page_round(segment.memory_size);
    if (!i) {
      first = address;
    }
    if (image->library && address > previous_end) {
      status = vm_reserve_at(space, previous_end, address - previous_end);
      if (status != MM_OK) {
        break;
      }
      reserved[reserved_count++] = (struct image_reservation){previous_end,
          address - previous_end, false};
    }
    status = vm_reserve_at(space, address, size);
    if (status != MM_OK) {
      break;
    }
    status = back_private(process, address, size);
    if (status != MM_OK) {
      KASSERT(vm_release(space, address, size) == MM_OK);
      break;
    }
    reserved[reserved_count++] = (struct image_reservation){address, size, true};
    copy_segment(process, address, (const uint8_t *)bytes + payload, segment.file_size);
    payload += segment.file_size;
    unsigned permissions = PAGE_USER |
        ((segment.flags & P1F_WRITE) ? PAGE_WRITE : 0) |
        ((segment.flags & P1F_EXECUTE) ? PAGE_EXEC : 0);
    status = protect_range(process, address, size, permissions);
    if (segment.flags & P1D_RELRO) {
      relro[relro_index++] = (struct private_image_range){address, size};
    }
    previous_end = address + size;
  }
  if (status != MM_OK) {
    unwind_image(process, reserved, reserved_count);
    kfree(reserved);
    kfree(relro);
    kfree(allocation);
    return jit_mm_status(status);
  }
  kfree(reserved);

  *allocation = (struct private_allocation){.next = process->allocations,
    .address = first, .size = previous_end - first, .identity = identity,
    .kind = PRIVATE_IMAGE, .image_relro = relro, .image_relro_count = relro_count};
  process->allocations = allocation;
  *result = (struct memory_image){
    .identity = identity,
    .base = base,
    .entry = image->entry,
    .dynamic = base + image->dynamic_address,
  };
  return CALL_OK;
}

enum call_status private_image_seal(struct process *process, uint64_t identity)
{
  process_vm_require_mutation(process);
  struct private_allocation *allocation = process->allocations;
  while (allocation && (allocation->kind != PRIVATE_IMAGE || allocation->identity != identity)) {
    allocation = allocation->next;
  }
  if (!identity || !allocation || allocation->image_sealed) {
    return CALL_BAD_REQUEST;
  }
  for (size_t i = 0; i < allocation->image_relro_count; ++i) {
    const struct private_image_range *range = &allocation->image_relro[i];
    enum mm_result status = protect_range(process, range->address, range->size, PAGE_USER);
    if (status != MM_OK) {
      return jit_mm_status(status);
    }
  }
  allocation->image_sealed = true;
  return CALL_OK;
}
