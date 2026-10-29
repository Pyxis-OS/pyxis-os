#include <kernel/mm/scanout.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/log.h>
#include <arch/paging.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>

#define UEFI_RESERVED_MEMORY_TYPE 0u
#define SCANOUT_ALIGNMENT (64 * 1024u)
#define SCANOUT_VGA_BYTES (9 * 1024 * 1024u)
#define SCANOUT_TAIL_BYTES (16 * 1024 * 1024u)

static bool reserved_pool(const struct boot_info *boot, phys_addr_t pool, size_t bytes)
{
  if (!boot->efi_map_valid || bytes > UINT64_MAX - pool) {
    return false;
  }
  bool boot_reserved = false, efi_reserved = false;
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if (r->length > UINT64_MAX - r->base) {
      return false;
    }
    if (r->base < pool + bytes && pool < r->base + r->length) {
      if (r->type != BOOT_RESERVED) {
        return false;
      }
      boot_reserved |= r->base <= pool && r->base + r->length >= pool + bytes;
    }
  }
  for (size_t i = 0; i < boot->efi_region_count; ++i) {
    const struct boot_efi_region *r = &boot->efi_regions[i];
    if (r->length > UINT64_MAX - r->base) {
      return false;
    }
    if (r->base < pool + bytes && pool < r->base + r->length) {
      if (r->type != UEFI_RESERVED_MEMORY_TYPE) {
        return false;
      }
      efi_reserved |= r->base <= pool && r->base + r->length >= pool + bytes;
    }
  }
  return boot_reserved && efi_reserved;
}

bool scanout_candidate(const struct boot_info *boot, phys_addr_t pool, size_t pool_bytes,
    size_t occupied_end, size_t payload, struct scanout_storage *storage)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count());
  *storage = (struct scanout_storage){0};
  size_t prefix = occupied_end > SCANOUT_VGA_BYTES ? occupied_end : SCANOUT_VGA_BYTES;
  if (!payload || prefix > SIZE_MAX - SCANOUT_ALIGNMENT + 1 ||
      payload > SIZE_MAX - SCANOUT_ALIGNMENT + 1 || pool_bytes <= SCANOUT_TAIL_BYTES) {
    return false;
  }
  size_t offset = (prefix + SCANOUT_ALIGNMENT - 1) & ~(size_t)(SCANOUT_ALIGNMENT - 1);
  size_t bytes = (payload + SCANOUT_ALIGNMENT - 1) & ~(size_t)(SCANOUT_ALIGNMENT - 1);
  if (offset > pool_bytes - SCANOUT_TAIL_BYTES || bytes > pool_bytes - SCANOUT_TAIL_BYTES - offset ||
      !reserved_pool(boot, pool, pool_bytes) ||
      vm_kernel_physical_overlap(pool + offset, bytes) ||
      paging_display_aperture_overlaps(pool + offset, bytes) ||
      (boot->framebuffer.physical < pool + offset + bytes &&
       pool + offset < boot->framebuffer.physical + boot->framebuffer.size)) {
    return false;
  }
  *storage = (struct scanout_storage){.physical = pool + offset, .offset = offset, .bytes = bytes};
  return true;
}

bool scanout_prepare(const struct boot_info *boot, phys_addr_t pool, size_t pool_bytes,
    size_t gop_end, size_t payload, struct scanout_storage *storage)
{
  if (!scanout_candidate(boot, pool, pool_bytes, gop_end, payload, storage)) {
    return false;
  }
  size_t bytes = storage->bytes;
  uintptr_t address;
  if (vm_reserve(vm_kernel_space(), bytes, SCANOUT_ALIGNMENT, &address) != MM_OK) {
    *storage = (struct scanout_storage){0};
    return false;
  }
  size_t mapped = 0;
  for (; mapped < bytes; mapped += PAGE_SIZE) {
    if (vm_map_scanout(address + mapped, storage->physical + mapped) != MM_OK) {
      break;
    }
  }
  if (mapped != bytes) {
    while (mapped) {
      mapped -= PAGE_SIZE;
      phys_addr_t physical;
      KASSERT(vm_unmap(vm_kernel_space(), address + mapped, &physical) == MM_OK);
    }
    KASSERT(vm_release(vm_kernel_space(), address, bytes) == MM_OK);
    *storage = (struct scanout_storage){0};
    return false;
  }
  storage->address = address;
  return true;
}

void scanout_discard(struct scanout_storage *storage)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count());
  for (size_t offset = 0; offset < storage->bytes; offset += PAGE_SIZE) {
    phys_addr_t physical;
    KASSERT(vm_unmap(vm_kernel_space(), storage->address + offset, &physical) == MM_OK);
  }
  KASSERT(vm_release(vm_kernel_space(), storage->address, storage->bytes) == MM_OK);
  *storage = (struct scanout_storage){0};
}

static size_t frame_batch(size_t remaining)
{
  return remaining < PMM_FRAME_BATCH_MAX ? remaining : PMM_FRAME_BATCH_MAX;
}

bool scanout_pages_prepare(size_t payload, uint64_t deadline, struct scanout_pages *storage)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count());
  *storage = (struct scanout_pages){0};
  if (!payload || payload > SIZE_MAX - PAGE_SIZE + 1) {
    return false;
  }
  size_t bytes = (payload + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
  size_t count = bytes / PAGE_SIZE;
  phys_addr_t *frames = kmalloc(count * sizeof(*frames));
  uintptr_t address;
  if (!frames) {
    return false;
  }
  if (vm_reserve(vm_kernel_space(), bytes, SCANOUT_ALIGNMENT, &address) != MM_OK) {
    kfree(frames);
    return false;
  }
  size_t allocated = 0, mapped = 0;
  uint64_t phase = arch_monotonic_ns(), transition_ns = 0, map_ns = 0;
  while (allocated < count && arch_monotonic_ns() < deadline) {
    size_t batch = frame_batch(count - allocated);
    size_t got = pmm_alloc_frames(frames + allocated, batch);
    allocated += got;
    if (got != batch) {
      break;
    }
  }
  uint64_t allocation_ns = arch_monotonic_ns() - phase;
  if (allocated == count) {
    while (mapped < count) {
      size_t batch = frame_batch(count - mapped);
      phase = arch_monotonic_ns();
      if (phase >= deadline ||
          !arch_frames_prepare_scanout(frames + mapped, batch)) {
        break;
      }
      transition_ns += arch_monotonic_ns() - phase;
      size_t end = mapped + batch;
      phase = arch_monotonic_ns();
      while (mapped < end) {
        size_t done;
        enum mm_result status = vm_map_scanout_run(address + mapped * PAGE_SIZE,
            frames + mapped, end - mapped, &done);
        mapped += done;
        if (status != MM_OK) {
          goto cleanup;
        }
      }
      map_ns += arch_monotonic_ns() - phase;
    }
  }
cleanup:
  if (mapped != count) {
    while (mapped) {
      --mapped;
      phys_addr_t physical;
      KASSERT(vm_unmap(vm_kernel_space(), address + mapped * PAGE_SIZE, &physical) == MM_OK);
    }
    for (size_t i = 0; i < allocated; i += PMM_FRAME_BATCH_MAX) {
      pmm_free_frames(frames + i, frame_batch(allocated - i));
    }
    KASSERT(vm_release(vm_kernel_space(), address, bytes) == MM_OK);
    kfree(frames);
    return false;
  }
  phase = arch_monotonic_ns();
  memset((void *)address, 0, bytes);
  cpu_store_fence();
  kdebug("scanout-prepare: frames=%zu bytes=%zu allocation-us=%lu cache-us=%lu map-us=%lu zero-us=%lu\n",
      count, bytes, allocation_ns / 1000, transition_ns / 1000, map_ns / 1000,
      (arch_monotonic_ns() - phase) / 1000);
  *storage = (struct scanout_pages){address, frames, bytes};
  return true;
}

void scanout_pages_discard(struct scanout_pages *storage)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count());
  cpu_store_fence();
  size_t count = storage->bytes / PAGE_SIZE;
  for (size_t i = 0; i < count; ++i) {
    phys_addr_t physical;
    KASSERT(vm_unmap(vm_kernel_space(), storage->address + i * PAGE_SIZE, &physical) == MM_OK);
  }
  KASSERT(vm_release(vm_kernel_space(), storage->address, storage->bytes) == MM_OK);
  for (size_t i = 0; i < count; i += PMM_FRAME_BATCH_MAX) {
    pmm_free_frames(storage->frames + i, frame_batch(count - i));
  }
  kfree(storage->frames);
  *storage = (struct scanout_pages){0};
}
