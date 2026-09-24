#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>

static void require_owner(const struct pci_claim *claim)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(claim && claim->device && claim->device->owner == claim);
}

void pci_write8(struct pci_claim *claim, unsigned offset, uint8_t value)
{
  require_owner(claim);
  arch_pci_write8(claim->device->address, offset, value);
}

void pci_write16(struct pci_claim *claim, unsigned offset, uint16_t value)
{
  require_owner(claim);
  arch_pci_write16(claim->device->address, offset, value);
}

void pci_write32(struct pci_claim *claim, unsigned offset, uint32_t value)
{
  require_owner(claim);
  arch_pci_write32(claim->device->address, offset, value);
}

bool pci_capability_fits(const struct pci_claim *claim, unsigned offset, size_t bytes)
{
  if (offset < PCI_CAP_FIRST || offset >= PCI_CONVENTIONAL_BYTES ||
      bytes < 2 || bytes > PCI_CONVENTIONAL_BYTES - offset) {
    return false;
  }
  bool found = false;
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned other = claim->capabilities[i];
    found |= other == offset;
    if (other > offset && other - offset < bytes) {
      return false;
    }
  }
  return found;
}

static bool capture_capabilities(struct pci_address address, struct pci_claim *claim)
{
  if (!(pci_read16(address, PCI_STATUS) & PCI_STATUS_CAPABILITIES)) {
    return true;
  }
  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned offset = pci_read8(address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  while (offset) {
    if (offset < PCI_CAP_FIRST || seen[offset / PCI_REGISTER_BYTES]) {
      return false;
    }
    seen[offset / PCI_REGISTER_BYTES] = true;
    claim->capabilities[claim->capability_count++] = offset;
    offset = pci_read8(address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }

  for (unsigned i = 0; i < claim->capability_count; ++i) {
    offset = claim->capabilities[i];
    unsigned id = pci_read8(address, offset);
    if (id == PCI_CAP_MSI || id == PCI_CAP_MSIX) {
      size_t bytes = id == PCI_CAP_MSIX ? PCI_MSIX_BYTES : 4;
      if (!pci_capability_fits(claim, offset, bytes)) {
        return false;
      }
      uint16_t control = pci_read16(address, offset + PCI_MSI_CONTROL);
      unsigned enabled = id == PCI_CAP_MSIX ? PCI_MSIX_ENABLE : PCI_MSI_ENABLE;
      if (control & enabled) {
        return false;
      }
    }
  }
  return true;
}

bool pci_claim_device(struct pci_device *device, struct pci_claim *claim)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(claim && !claim->device && !claim->mappings);
  if (!device || device->owner || device->header_type != PCI_HEADER_ENDPOINT) {
    return false;
  }
  *claim = (struct pci_claim){0};
  uint16_t command = pci_read16(device->address, PCI_COMMAND);
  if (!capture_capabilities(device->address, claim)) {
    return false;
  }

  claim->device = device;
  claim->saved_command = command;
  device->owner = claim;
  arch_pci_config_writable(device->address, true);
  /* The adjacent status word has write-one-to-clear bits: command updates
   * must be 16-bit writes, never a read/modify/write of the entire dword. */
  pci_write16(claim, PCI_COMMAND,
              (command & ~PCI_COMMAND_MASTER) | PCI_COMMAND_INTX_DISABLE);
  return true;
}

void pci_release_device(struct pci_claim *claim)
{
  require_owner(claim);
  /* This boot-only unwind cannot be used after a driver starts DMA. */
  KASSERT(!(pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER));
  while (claim->mappings) {
    struct pci_mapping *mapping = claim->mappings;
    claim->mappings = mapping->next;
    for (size_t offset = 0; offset < mapping->bytes; offset += PAGE_SIZE) {
      phys_addr_t physical;
      KASSERT(vm_unmap(vm_kernel_space(), mapping->base + offset, &physical) == MM_OK);
    }
    KASSERT(vm_release(vm_kernel_space(), mapping->base, mapping->bytes) == MM_OK);
    *mapping = (struct pci_mapping){0};
  }
  pci_write16(claim, PCI_COMMAND,
              (claim->saved_command & ~PCI_COMMAND_MASTER) | PCI_COMMAND_INTX_DISABLE);
  arch_pci_config_writable(claim->device->address, false);
  claim->device->owner = NULL;
  *claim = (struct pci_claim){0};
}

bool pci_size_bars(struct pci_claim *claim)
{
  require_owner(claim);
  struct pci_address address = claim->device->address;
  KASSERT(!(pci_read16(address, PCI_COMMAND) &
            (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)));

  for (unsigned bar = 0; bar < PCI_BAR_COUNT; ++bar) {
    unsigned offset = PCI_BAR_FIRST + bar * PCI_REGISTER_BYTES;
    uint32_t low = pci_read32(address, offset);
    if (low & PCI_BAR_IO) {
      continue; /* No port-I/O consumer yet. Its assignment stays untouched. */
    }
    unsigned type = low & PCI_BAR_MEMORY_TYPE_MASK;
    bool wide = type == PCI_BAR_MEMORY_64;
    if ((type != PCI_BAR_MEMORY_32 && !wide) || (wide && bar + 1 == PCI_BAR_COUNT)) {
      return false;
    }
    uint32_t high = wide ? pci_read32(address, offset + PCI_REGISTER_BYTES) : 0;
    pci_write32(claim, offset, UINT32_MAX);
    if (wide) {
      pci_write32(claim, offset + PCI_REGISTER_BYTES, UINT32_MAX);
    }
    uint32_t mask_low = pci_read32(address, offset);
    uint32_t mask_high = wide ? pci_read32(address, offset + PCI_REGISTER_BYTES) : 0;
    /* Restore both halves before any validation can return to the caller. */
    if (wide) {
      pci_write32(claim, offset + PCI_REGISTER_BYTES, high);
    }
    pci_write32(claim, offset, low);

    uint64_t mask = ((uint64_t)mask_high << PCI_BAR_HIGH_SHIFT) |
      (mask_low & PCI_BAR_MEMORY_ADDRESS_MASK);
    uint64_t base = ((uint64_t)high << PCI_BAR_HIGH_SHIFT) |
      (low & PCI_BAR_MEMORY_ADDRESS_MASK);
    if (!mask && !low) {
      continue;
    }
    if (!wide) {
      mask |= UINT64_C(0xffffffff00000000);
    }
    uint64_t bytes = ~mask + 1;
    if (!bytes || (bytes & (bytes - 1)) || !base || (base & (bytes - 1)) ||
        bytes > UINT64_MAX - base ||
        (mask_low & ~PCI_BAR_MEMORY_ADDRESS_MASK) != (low & ~PCI_BAR_MEMORY_ADDRESS_MASK)) {
      return false;
    }
    for (unsigned other = 0; other < bar; ++other) {
      const struct pci_bar *previous = &claim->bars[other];
      if (previous->bytes && base < previous->physical + previous->bytes &&
          previous->physical < base + bytes) {
        return false;
      }
    }
    claim->bars[bar] = (struct pci_bar){.physical = base, .bytes = bytes};
    klog("  PCI owned BAR%u: base=0x%lx bytes=0x%lx\n", bar, base, bytes);
    if (wide) {
      ++bar;
    }
  }
  return true;
}

enum mm_result pci_map_bar(struct pci_claim *claim, unsigned bar, uint64_t offset,
                           size_t bytes, const struct boot_info *boot,
                           struct pci_mapping *mapping)
{
  require_owner(claim);
  KASSERT(mapping && !mapping->base);
  if (bar >= PCI_BAR_COUNT || !bytes || offset > claim->bars[bar].bytes ||
      bytes > claim->bars[bar].bytes - offset) {
    return MM_INVALID;
  }
  phys_addr_t physical = claim->bars[bar].physical + offset;
  size_t page_offset = physical & (PAGE_SIZE - 1);
  if (bytes > SIZE_MAX - page_offset - (PAGE_SIZE - 1)) {
    return MM_INVALID;
  }
  size_t extent = (page_offset + bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  phys_addr_t first = physical - page_offset;
  if (extent > UINT64_MAX - first || !arch_pci_mmio_available(first, extent)) {
    return MM_INVALID;
  }
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (region->base < first + extent && first < region->base + region->length &&
        region->type != BOOT_RESERVED) {
      return MM_INVALID;
    }
  }

  uintptr_t base;
  enum mm_result result = vm_reserve(vm_kernel_space(), extent, PAGE_SIZE, &base);
  if (result != MM_OK) {
    return result;
  }
  size_t mapped = 0;
  for (; mapped < extent; mapped += PAGE_SIZE) {
    result = vm_map_mmio(base + mapped, first + mapped);
    if (result != MM_OK) {
      break;
    }
  }
  if (mapped != extent) {
    while (mapped) {
      mapped -= PAGE_SIZE;
      phys_addr_t frame;
      KASSERT(vm_unmap(vm_kernel_space(), base + mapped, &frame) == MM_OK);
    }
    KASSERT(vm_release(vm_kernel_space(), base, extent) == MM_OK);
    return result;
  }
  *mapping = (struct pci_mapping){
    .address = base + page_offset, .base = base, .bytes = extent, .next = claim->mappings,
  };
  claim->mappings = mapping;
  return MM_OK;
}
