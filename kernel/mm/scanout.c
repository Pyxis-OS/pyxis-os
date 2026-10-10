#include <kernel/mm/scanout.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <arch/paging.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>

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
