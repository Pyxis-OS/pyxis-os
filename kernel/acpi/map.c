#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include "host.h"

/* Address space only: page tables appear as pages are mapped. AML maps whole
 * operation regions, so a single request can be large. */
#define MAP_WINDOW_BYTES (UINT64_C(64) << 20)

enum memory_class {
  MEMORY_REFUSED,
  MEMORY_CACHED,
  MEMORY_DEVICE,
};

struct firmware_mapping {
  struct firmware_mapping *next;
  phys_addr_t physical;
  size_t bytes;
  uintptr_t address;
};

/* The window is reserved before AP startup and filled in address order. An
 * address is mapped at most once, so no CPU can hold a stale translation for
 * it and mapping needs no shootdown. Worker-only after preparation. */
static struct boot_region *regions;
static size_t region_count;
static uintptr_t window_base;
static size_t window_used;
static struct firmware_mapping *mappings;

bool acpi_map_prepare(const struct boot_info *boot)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (boot->region_count > SIZE_MAX / sizeof(*regions)) {
    klog("ACPI: firmware memory map too large; ACPI unavailable\n");
    return false;
  }
  regions = kmalloc(boot->region_count * sizeof(*regions));
  if (!regions) {
    klog("ACPI: cannot copy the firmware memory map; ACPI unavailable\n");
    return false;
  }
  memcpy(regions, boot->regions, boot->region_count * sizeof(*regions));
  region_count = boot->region_count;

  enum mm_result result = vm_reserve(vm_kernel_space(), MAP_WINDOW_BYTES, PAGE_SIZE,
                                     &window_base);
  if (result != MM_OK) {
    kfree(regions);
    regions = NULL;
    region_count = 0;
    klog("ACPI: cannot reserve the firmware mapping window (error %u); ACPI unavailable\n",
         (unsigned)result);
    return false;
  }
  return true;
}

static enum memory_class page_class(phys_addr_t page)
{
  for (size_t i = 0; i < region_count; ++i) {
    const struct boot_region *region = &regions[i];
    if (page < region->base || page - region->base >= region->length) {
      continue;
    }
    switch (region->type) {
    case BOOT_ACPI:
    case BOOT_FIRMWARE:
      return MEMORY_CACHED;
    case BOOT_RESERVED:
      return MEMORY_DEVICE;
    default:
      return MEMORY_REFUSED;
    }
  }
  return MEMORY_DEVICE;
}

/* The whole extent must share one class, so one request never mixes cache types. */
static enum memory_class extent_class(phys_addr_t first, size_t bytes)
{
  enum memory_class class = page_class(first);
  for (size_t offset = PAGE_SIZE; offset < bytes; offset += PAGE_SIZE) {
    if (page_class(first + offset) != class) {
      return MEMORY_REFUSED;
    }
  }
  return class;
}

static const struct firmware_mapping *find_mapping(phys_addr_t first, size_t bytes)
{
  for (const struct firmware_mapping *mapping = mappings; mapping; mapping = mapping->next) {
    if (mapping->physical <= first && first - mapping->physical <= mapping->bytes &&
        bytes <= mapping->bytes - (first - mapping->physical)) {
      return mapping;
    }
  }
  return NULL;
}

/* IF=0. Consumes the window range even on failure, so addresses that may have
 * been mapped briefly are never handed out again. */
static void *map_new(phys_addr_t first, size_t bytes, enum memory_class class)
{
  struct firmware_mapping *mapping = kmalloc(sizeof(*mapping));
  if (!mapping) {
    return NULL;
  }

  uintptr_t base = window_base + window_used;
  window_used += bytes;
  size_t mapped = 0;
  enum mm_result result = MM_OK;
  for (; mapped < bytes; mapped += PAGE_SIZE) {
    if (class == MEMORY_CACHED) {
      result = vm_map(vm_kernel_space(), base + mapped, first + mapped, PAGE_WRITE);
    } else {
      result = vm_map_mmio(base + mapped, first + mapped);
    }
    if (result != MM_OK) {
      break;
    }
  }
  if (result != MM_OK) {
    while (mapped) {
      mapped -= PAGE_SIZE;
      phys_addr_t frame;
      KASSERT(vm_unmap(vm_kernel_space(), base + mapped, &frame) == MM_OK);
    }
    kfree(mapping);
    klog("ACPI: cannot map physical 0x%lx (%zu bytes, error %u)\n",
         first, bytes, (unsigned)result);
    return NULL;
  }

  *mapping = (struct firmware_mapping){
    .next = mappings,
    .physical = first,
    .bytes = bytes,
    .address = base,
  };
  mappings = mapping;
  return (void *)base;
}

void *acpi_map(phys_addr_t physical, size_t bytes)
{
  acpi_require_worker();
  size_t offset = physical & (PAGE_SIZE - 1);
  phys_addr_t first = physical - offset;
  if (!window_base || !bytes || bytes > SIZE_MAX - offset - (PAGE_SIZE - 1)) {
    return NULL;
  }
  size_t extent = (offset + bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  if (extent - 1 > UINT64_MAX - first) {
    return NULL;
  }

  uint64_t flags = cpu_save_interrupts();
  void *address = NULL;
  const struct firmware_mapping *existing = find_mapping(first, extent);
  if (existing) {
    address = (void *)(existing->address + (first - existing->physical));
  } else {
    enum memory_class class = extent_class(first, extent);
    if (class == MEMORY_REFUSED) {
      klog("ACPI: refused mapping of physical 0x%lx (%zu bytes): RAM or mixed memory types\n",
           physical, bytes);
    } else if (extent > MAP_WINDOW_BYTES - window_used) {
      klog("ACPI: firmware mapping window full; cannot map physical 0x%lx (%zu bytes)\n",
           physical, bytes);
    } else {
      address = map_new(first, extent, class);
    }
  }
  cpu_restore_interrupts(flags);
  return address ? (uint8_t *)address + offset : NULL;
}

void acpi_unmap(void *address, size_t bytes)
{
  acpi_require_worker();
  uintptr_t start = (uintptr_t)address;
  KASSERT(start >= window_base && start - window_base < window_used &&
          bytes <= window_used - (start - window_base));
}

size_t acpi_map_pages(void)
{
  return window_used / PAGE_SIZE;
}
