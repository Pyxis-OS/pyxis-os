#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/paging.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>

#define PCI_POWER_D0_WAIT_MS 10

static void require_owner(const struct pci_claim *claim)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(claim && claim->device && claim->device->owner == claim);
}

void pci_write8(struct pci_claim *claim, unsigned offset, uint8_t value)
{
  require_owner(claim);
  KASSERT(offset != PCI_COMMAND && offset != PCI_COMMAND + 1);
  arch_pci_write8(claim->device->address, offset, value);
}

void pci_write16(struct pci_claim *claim, unsigned offset, uint16_t value)
{
  require_owner(claim);
  KASSERT(offset != PCI_COMMAND || !claim->reserved);
  if (offset == PCI_COMMAND && !claim->reserved && (value & PCI_COMMAND_MASTER)) {
    claim->dma_started = true;
  }
  arch_pci_write16(claim->device->address, offset, value);
}

void pci_write32(struct pci_claim *claim, unsigned offset, uint32_t value)
{
  require_owner(claim);
  KASSERT(offset != PCI_COMMAND);
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

bool pci_reserve_device(struct pci_device *device, struct pci_claim *claim)
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
  claim->reserved = true;
  device->owner = claim;
  arch_pci_config_writable(device->address, true);
  return true;
}

bool pci_begin_mmio_probe(struct pci_claim *claim, struct pci_probe_state *state)
{
  require_owner(claim);
  KASSERT(claim->reserved && !claim->mappings);
  KASSERT(state && !state->command_changed && !state->power_changed);
  *state = (struct pci_probe_state){0};
  struct pci_address address = claim->device->address;
  state->command = pci_read16(address, PCI_COMMAND);
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    if (pci_read8(address, offset) != PCI_CAP_POWER) {
      continue;
    }
    if (state->power_capability || !pci_capability_fits(claim, offset, PCI_POWER_BYTES)) {
      return false;
    }
    state->power_capability = offset;
  }
  if (state->power_capability) {
    state->pmcsr = pci_read16(address, state->power_capability + PCI_POWER_CONTROL);
  }
  bool wake = (state->pmcsr & PCI_POWER_STATE_MASK) != PCI_POWER_D0;
  if (!wake && (state->command & PCI_COMMAND_MEMORY)) {
    return true;
  }
  if (state->command & PCI_COMMAND_MASTER) {
    return false;
  }
  /* A reset-causing D3hot wake can discard firmware BAR assignments. */
  if ((state->pmcsr & PCI_POWER_STATE_MASK) == PCI_POWER_D3HOT &&
      !(state->pmcsr & PCI_POWER_NO_SOFT_RESET)) {
    return false;
  }
  if (wake) {
    state->power_changed = true;
    pci_write16(claim, state->power_capability + PCI_POWER_CONTROL,
                state->pmcsr & ~(PCI_POWER_STATE_MASK | PCI_POWER_PME_STATUS));
    uint64_t deadline = task_deadline_after_ms(PCI_POWER_D0_WAIT_MS);
    do {
      arch_clock_maintain();
      __asm__ volatile("pause");
    } while (!task_deadline_expired(deadline));
    uint16_t pmcsr = pci_read16(address, state->power_capability + PCI_POWER_CONTROL);
    uint16_t expected = state->pmcsr & ~PCI_POWER_STATE_MASK;
    if ((pmcsr & PCI_POWER_CONTROL_WRITABLE) != (expected & PCI_POWER_CONTROL_WRITABLE)) {
      return false;
    }
  }
  uint16_t command = pci_read16(address, PCI_COMMAND);
  if (command & PCI_COMMAND_MASTER) {
    return false;
  }
  uint16_t expected = state->command | PCI_COMMAND_MEMORY;
  if (command != expected) {
    state->command_changed = true;
    /* The generic reserved-claim command-write ban remains in force. Only
     * this PCI-owned probe path may restore the snapshot and enable memory. */
    arch_pci_write16(address, PCI_COMMAND, expected);
  }
  return pci_read16(address, PCI_COMMAND) == expected;
}

bool pci_restore_mmio_probe(struct pci_claim *claim, struct pci_probe_state *state)
{
  require_owner(claim);
  KASSERT(claim->reserved && state);
  if (!state->command_changed && !state->power_changed) {
    return true;
  }
  struct pci_address address = claim->device->address;
  uint16_t command = pci_read16(address, PCI_COMMAND);
  if ((command | state->command) & PCI_COMMAND_MASTER) {
    return false;
  }
  if (state->command_changed || command != state->command) {
    state->command_changed = true;
    arch_pci_write16(address, PCI_COMMAND, state->command);
    if (pci_read16(address, PCI_COMMAND) != state->command) {
      return false;
    }
    state->command_changed = false;
  }
  if (state->power_changed) {
    pci_write16(claim, state->power_capability + PCI_POWER_CONTROL,
                state->pmcsr & PCI_POWER_CONTROL_WRITABLE);
    uint16_t pmcsr = pci_read16(address, state->power_capability + PCI_POWER_CONTROL);
    if ((pmcsr & PCI_POWER_CONTROL_WRITABLE) != (state->pmcsr & PCI_POWER_CONTROL_WRITABLE) ||
        pci_read16(address, PCI_COMMAND) != state->command) {
      return false;
    }
    state->power_changed = false;
  }
  return true;
}

static bool disable_dma_and_intx(struct pci_claim *claim)
{
  require_owner(claim);
  KASSERT(claim->reserved);
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  claim->reserved = false;
  /* The adjacent status word has write-one-to-clear bits: command updates
   * must be 16-bit writes, never a read/modify/write of the entire dword. */
  pci_write16(claim, PCI_COMMAND,
              (command & ~PCI_COMMAND_MASTER) | PCI_COMMAND_INTX_DISABLE);
  command = pci_read16(claim->device->address, PCI_COMMAND);
  return (command & (PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE)) == PCI_COMMAND_INTX_DISABLE;
}

bool pci_complete_claim(struct pci_claim *claim)
{
  return disable_dma_and_intx(claim);
}

bool pci_claim_device(struct pci_device *device, struct pci_claim *claim)
{
  if (!pci_reserve_device(device, claim)) {
    return false;
  }
  /* The existing immediate path disables DMA before its transport resets through
   * PCI configuration access. MMIO consumers use staged completion after halt. */
  if (!disable_dma_and_intx(claim)) {
    klog("PCI: cannot disable DMA/INTx; claim retained until reboot\n");
    return false;
  }
  return true;
}

static void unmap_resources(struct pci_claim *claim)
{
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
}

static void withdraw_claim(struct pci_claim *claim)
{
  arch_pci_config_writable(claim->device->address, false);
  claim->device->owner = NULL;
  *claim = (struct pci_claim){0};
}

void pci_cancel_reservation(struct pci_claim *claim)
{
  require_owner(claim);
  KASSERT(claim->reserved && !claim->dma_started);
  /* Firmware may still own an active controller. Only CPU mappings/config
   * permissions were borrowed; cancellation must not write its command word. */
  unmap_resources(claim);
  withdraw_claim(claim);
}

void pci_release_device(struct pci_claim *claim)
{
  require_owner(claim);
  KASSERT(!claim->reserved && !claim->dma_started);
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  KASSERT((command & (PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE)) == PCI_COMMAND_INTX_DISABLE);
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    unsigned id = pci_read8(claim->device->address, offset);
    if (id == PCI_CAP_MSI || id == PCI_CAP_MSIX) {
      unsigned enabled = id == PCI_CAP_MSIX ? PCI_MSIX_ENABLE : PCI_MSI_ENABLE;
      KASSERT(!(pci_read16(claim->device->address, offset + PCI_MSI_CONTROL) & enabled));
    }
  }
  unmap_resources(claim);
  pci_write16(claim, PCI_COMMAND,
              (claim->saved_command & ~PCI_COMMAND_MASTER) | PCI_COMMAND_INTX_DISABLE);
  withdraw_claim(claim);
}

bool pci_size_bars(struct pci_claim *claim)
{
  require_owner(claim);
  KASSERT(!claim->reserved && !claim->dma_started);
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
    ktrace("  PCI owned BAR%u: base=0x%lx bytes=0x%lx\n", bar, base, bytes);
    if (wide) {
      ++bar;
    }
  }
  return true;
}

static enum mm_result map_resource(struct pci_claim *claim, phys_addr_t physical,
    size_t bytes, const struct boot_info *boot, struct pci_mapping *mapping)
{
  KASSERT(mapping && !mapping->base);
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

enum mm_result pci_map_bootstrap_bar(struct pci_claim *claim, unsigned bar,
    const struct boot_info *boot, struct pci_mapping *mapping)
{
  require_owner(claim);
  KASSERT(claim->reserved && !claim->mappings);
  if (bar >= PCI_BAR_COUNT) {
    return MM_INVALID;
  }
  struct pci_address address = claim->device->address;
  if (!(pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MEMORY)) {
    return MM_INVALID;
  }
  /* Walk low halves so address bits in a paired upper half are never decoded
   * as another BAR's type. */
  for (unsigned previous = 0; previous < bar; ++previous) {
    uint32_t low = pci_read32(address, PCI_BAR_FIRST + previous * PCI_REGISTER_BYTES);
    if (!(low & PCI_BAR_IO) && (low & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_64) {
      if (previous + 1 == bar) {
        return MM_INVALID;
      }
      ++previous;
    }
  }
  unsigned offset = PCI_BAR_FIRST + bar * PCI_REGISTER_BYTES;
  uint32_t low = pci_read32(address, offset);
  unsigned type = low & PCI_BAR_MEMORY_TYPE_MASK;
  bool wide = type == PCI_BAR_MEMORY_64;
  if ((low & PCI_BAR_IO) || (type != PCI_BAR_MEMORY_32 && !wide) ||
      (wide && bar + 1 == PCI_BAR_COUNT)) {
    return MM_INVALID;
  }
  uint32_t high = wide ? pci_read32(address, offset + PCI_REGISTER_BYTES) : 0;
  phys_addr_t physical = ((uint64_t)high << PCI_BAR_HIGH_SHIFT) |
    (low & PCI_BAR_MEMORY_ADDRESS_MASK);
  if (!physical || (physical & (PAGE_SIZE - 1))) {
    return MM_INVALID;
  }
  return map_resource(claim, physical, PCI_BOOTSTRAP_BAR_BYTES, boot, mapping);
}

enum mm_result pci_map_bar(struct pci_claim *claim, unsigned bar, uint64_t offset,
    size_t bytes, const struct boot_info *boot, struct pci_mapping *mapping)
{
  require_owner(claim);
  KASSERT(!claim->reserved && !claim->dma_started);
  if (bar >= PCI_BAR_COUNT || !bytes || offset > claim->bars[bar].bytes ||
      bytes > claim->bars[bar].bytes - offset) {
    return MM_INVALID;
  }
  return map_resource(claim, claim->bars[bar].physical + offset, bytes, boot, mapping);
}

enum mm_result pci_map_display_bar(struct pci_claim *claim, unsigned bar,
    const struct boot_info *boot, uintptr_t *address)
{
  require_owner(claim);
  KASSERT(!claim->reserved && !claim->dma_started);
  if (!boot || !address) {
    return MM_INVALID;
  }
  *address = 0;
  if (bar >= PCI_BAR_COUNT) {
    return MM_INVALID;
  }
  const struct pci_bar *resource = &claim->bars[bar];
  phys_addr_t first = resource->physical;
  if (!first || !resource->bytes || resource->bytes > SIZE_MAX ||
      (first & (PAGE_SIZE - 1)) || (resource->bytes & (PAGE_SIZE - 1)) ||
      resource->bytes > UINT64_MAX - first ||
      !arch_pci_mmio_available(first, resource->bytes)) {
    return MM_INVALID;
  }
  phys_addr_t end = first + resource->bytes;
  const struct boot_framebuffer *fb = &boot->framebuffer;
  if (fb->size && (fb->physical != first || fb->size > resource->bytes)) {
    return MM_INVALID;
  }
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (region->length > UINT64_MAX - region->base) {
      return MM_INVALID;
    }
    phys_addr_t region_end = region->base + region->length;
    if (region->base >= end || first >= region_end || region->type == BOOT_RESERVED) {
      continue;
    }
    if (region->type != BOOT_FRAMEBUFFER || !fb->size || region->base < first ||
        region_end > end || region->base >= fb->physical + fb->size ||
        fb->physical >= region_end) {
      return MM_INVALID;
    }
  }
  /* Existing PCI mappings are UC. Display memory must not alias
   * them with a different cache type, even when firmware reported it reserved. */
  for (size_t i = 0; i < pci_device_count(); ++i) {
    const struct pci_device *device = pci_device_at(i);
    if (!device->owner) {
      continue;
    }
    for (const struct pci_mapping *mapping = device->owner->mappings; mapping; mapping = mapping->next) {
      for (size_t offset = 0; offset < mapping->bytes; offset += PAGE_SIZE) {
        struct page_translation page;
        KASSERT(arch_page_query(arch_kernel_space(), mapping->base + offset, &page) == MM_OK);
        if (page.physical < end && first < page.physical + PAGE_SIZE) {
          return MM_INVALID;
        }
      }
    }
  }
  return paging_display_aperture(first, resource->bytes, fb, address);
}
