#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/panic.h>
#include <kernel/pci/msix.h>

#define PCI_MSIX_ROUTED_ENTRY 0

static void require_owner(const struct pci_msix *msix)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(msix && msix->claim && msix->claim->device && !msix->claim->reserved);
  KASSERT(msix->claim->device->owner == msix->claim && msix->capability);
}

static bool region_fits(const struct pci_claim *claim, const struct pci_region *region)
{
  return region->bar < PCI_BAR_COUNT && region->length &&
    region->offset <= claim->bars[region->bar].bytes &&
    region->length <= claim->bars[region->bar].bytes - region->offset;
}

bool pci_msix_discover(struct pci_claim *claim, struct pci_msix *msix)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(claim && claim->device && claim->device->owner == claim);
  KASSERT(!claim->reserved && !claim->dma_started && msix && !msix->claim);
  unsigned capability = 0;
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    if (pci_read8(claim->device->address, offset) != PCI_CAP_MSIX) {
      continue;
    }
    if (capability || !pci_capability_fits(claim, offset, PCI_MSIX_BYTES)) {
      return false;
    }
    capability = offset;
  }
  if (!capability) {
    return false;
  }
  struct pci_address address = claim->device->address;
  uint16_t control = pci_read16(address, capability + PCI_MSIX_CONTROL);
  if (control & PCI_MSIX_ENABLE) {
    return false;
  }
  unsigned entries = (control & PCI_MSIX_SIZE_MASK) + 1;
  uint32_t table = pci_read32(address, capability + PCI_MSIX_TABLE);
  uint32_t pba = pci_read32(address, capability + PCI_MSIX_PBA);
  struct pci_msix found = {
    .claim = claim, .capability = capability, .entries = entries,
    .table = {
      .bar = table & PCI_MSIX_BAR_MASK, .offset = table & PCI_MSIX_OFFSET_MASK,
      .length = entries * PCI_MSIX_ENTRY_BYTES,
    },
    .pba = {
      .bar = pba & PCI_MSIX_BAR_MASK, .offset = pba & PCI_MSIX_OFFSET_MASK,
      .length = ((entries + PCI_MSIX_PBA_BITS - 1) / PCI_MSIX_PBA_BITS) * PCI_MSIX_PBA_WORD_BYTES,
    },
  };
  if (!region_fits(claim, &found.table) || !region_fits(claim, &found.pba) ||
      (found.table.bar == found.pba.bar &&
       found.table.offset < (uint64_t)found.pba.offset + found.pba.length &&
       found.pba.offset < (uint64_t)found.table.offset + found.table.length)) {
    return false;
  }
  *msix = found;
  return true;
}

enum mm_result pci_msix_map(struct pci_msix *msix, const struct boot_info *boot)
{
  require_owner(msix);
  enum mm_result result = pci_map_bar(msix->claim, msix->table.bar,
      msix->table.offset, msix->table.length, boot, &msix->table.mapping);
  if (result != MM_OK) {
    return result;
  }
  return pci_map_bar(msix->claim, msix->pba.bar,
      msix->pba.offset, msix->pba.length, boot, &msix->pba.mapping);
}

bool pci_msix_prepare(struct pci_msix *msix, uint8_t vector)
{
  require_owner(msix);
  KASSERT(!msix->claim->dma_started && msix->table.mapping.address && msix->pba.mapping.address);
  struct pci_claim *claim = msix->claim;
  unsigned offset = msix->capability + PCI_MSIX_CONTROL;
  uint16_t control = pci_read16(claim->device->address, offset);
  unsigned masked_enable = PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK;
  pci_write16(claim, offset, control | masked_enable);
  if ((pci_read16(claim->device->address, offset) & masked_enable) != masked_enable) {
    return false;
  }

  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)msix->table.mapping.address;
  for (unsigned i = 0; i < msix->entries; ++i) {
    table[i].control |= PCI_MSIX_VECTOR_MASK;
    if (!(table[i].control & PCI_MSIX_VECTOR_MASK)) {
      return false;
    }
  }

  struct apic_msi_message message = apic_bsp_msi_message(vector);
  volatile struct pci_msix_entry *entry = &table[PCI_MSIX_ROUTED_ENTRY];
  entry->address_low = message.address_low;
  entry->address_high = message.address_high;
  entry->data = message.data;
  /* Individual uncached dword reads complete the device register writes. */
  return entry->address_low == message.address_low && entry->address_high == message.address_high &&
    entry->data == message.data && (entry->control & PCI_MSIX_VECTOR_MASK);
}

bool pci_msix_enable(struct pci_msix *msix)
{
  require_owner(msix);
  KASSERT(msix->table.mapping.address);
  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)msix->table.mapping.address;
  table[PCI_MSIX_ROUTED_ENTRY].control &= ~PCI_MSIX_VECTOR_MASK;
  if (table[PCI_MSIX_ROUTED_ENTRY].control & PCI_MSIX_VECTOR_MASK) {
    return false;
  }
  struct pci_claim *claim = msix->claim;
  unsigned offset = msix->capability + PCI_MSIX_CONTROL;
  uint16_t control = pci_read16(claim->device->address, offset);
  pci_write16(claim, offset, control & ~PCI_MSIX_FUNCTION_MASK);
  control = pci_read16(claim->device->address, offset);
  return (control & (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) == PCI_MSIX_ENABLE;
}

bool pci_msix_disable(struct pci_msix *msix)
{
  require_owner(msix);
  KASSERT(msix->table.mapping.address);
  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)msix->table.mapping.address;
  table[PCI_MSIX_ROUTED_ENTRY].control |= PCI_MSIX_VECTOR_MASK;
  bool masked = (table[PCI_MSIX_ROUTED_ENTRY].control & PCI_MSIX_VECTOR_MASK) != 0;
  struct pci_claim *claim = msix->claim;
  unsigned offset = msix->capability + PCI_MSIX_CONTROL;
  uint16_t control = pci_read16(claim->device->address, offset);
  pci_write16(claim, offset, (control | PCI_MSIX_FUNCTION_MASK) & ~PCI_MSIX_ENABLE);
  control = pci_read16(claim->device->address, offset);
  return masked &&
    (control & (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) == PCI_MSIX_FUNCTION_MASK;
}

bool pci_msix_mask_prepared_entry(const struct pci_msix *msix,
    unsigned entry, uint32_t *saved_control)
{
  if (!msix || !msix->table.mapping.address || entry >= msix->entries ||
      !saved_control) {
    return false;
  }
  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)msix->table.mapping.address;
  uint32_t control = table[entry].control;
  if (control == UINT32_MAX) {
    return false;
  }
  *saved_control = control;
  table[entry].control = control | PCI_MSIX_VECTOR_MASK;
  return table[entry].control == (control | PCI_MSIX_VECTOR_MASK);
}

bool pci_msix_restore_prepared_entry(const struct pci_msix *msix,
    unsigned entry, uint32_t saved_control)
{
  if (!msix || !msix->table.mapping.address || entry >= msix->entries ||
      saved_control == UINT32_MAX) {
    return false;
  }
  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)msix->table.mapping.address;
  table[entry].control = saved_control;
  return table[entry].control == saved_control;
}
