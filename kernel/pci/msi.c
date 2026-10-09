#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/panic.h>
#include <kernel/pci/msi.h>
#include <kernel/pci/registers.h>

#define MSI_64BIT (1u << 7)
#define MSI_VECTOR_MASK_CAPABLE (1u << 8)
#define MSI_MESSAGE_CAPACITY_MASK (7u << 1)
#define MSI_MESSAGE_ENABLE_MASK (7u << 4)
#define MSI_ADDRESS_LOW 4
#define MSI_ADDRESS_HIGH 8
#define MSI_DATA 12
#define MSI_64BIT_BYTES 14

static void require_claim(const struct pci_claim *claim)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(claim && claim->device && !claim->reserved && claim->device->owner == claim);
}

static bool intx_disabled(const struct pci_claim *claim)
{
  return pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_INTX_DISABLE;
}

bool pci_msi_prepare(struct pci_claim *claim, struct pci_msi *msi, uint8_t vector)
{
  require_claim(claim);
  KASSERT(!claim->dma_started && msi && !msi->claim);
  unsigned capability = 0;
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    if (pci_read8(claim->device->address, offset) != PCI_CAP_MSI) {
      continue;
    }
    if (capability || !pci_capability_fits(claim, offset, MSI_64BIT_BYTES)) {
      return false;
    }
    capability = offset;
  }
  if (!capability || !intx_disabled(claim)) {
    return false;
  }
  struct pci_address address = claim->device->address;
  unsigned control_offset = capability + PCI_MSI_CONTROL;
  uint16_t control = pci_read16(address, control_offset);
  unsigned layout_mask = MSI_64BIT | MSI_VECTOR_MASK_CAPABLE | MSI_MESSAGE_CAPACITY_MASK |
      PCI_MSI_ENABLE;
  if ((control & layout_mask) != MSI_64BIT) {
    return false;
  }
  pci_write16(claim, control_offset, control & ~MSI_MESSAGE_ENABLE_MASK);
  struct apic_msi_message message = apic_bsp_msi_message(vector);
  pci_write32(claim, capability + MSI_ADDRESS_LOW, message.address_low);
  pci_write32(claim, capability + MSI_ADDRESS_HIGH, message.address_high);
  pci_write16(claim, capability + MSI_DATA, message.data);
  if (pci_read32(address, capability + MSI_ADDRESS_LOW) != message.address_low ||
      pci_read32(address, capability + MSI_ADDRESS_HIGH) != message.address_high ||
      pci_read16(address, capability + MSI_DATA) != message.data ||
      (pci_read16(address, control_offset) & (layout_mask | MSI_MESSAGE_ENABLE_MASK)) != MSI_64BIT) {
    return false;
  }
  *msi = (struct pci_msi){ .claim = claim, .capability = capability };
  return true;
}

static bool set_enabled(struct pci_msi *msi, bool enabled)
{
  KASSERT(msi && msi->claim && msi->capability);
  require_claim(msi->claim);
  unsigned offset = msi->capability + PCI_MSI_CONTROL;
  struct pci_address address = msi->claim->device->address;
  uint16_t control = pci_read16(address, offset);
  control &= ~(PCI_MSI_ENABLE | MSI_MESSAGE_ENABLE_MASK);
  if (enabled) {
    control |= PCI_MSI_ENABLE;
  }
  pci_write16(msi->claim, offset, control);
  control = pci_read16(address, offset);
  return (control & (PCI_MSI_ENABLE | MSI_MESSAGE_ENABLE_MASK)) ==
      (enabled ? PCI_MSI_ENABLE : 0) && intx_disabled(msi->claim);
}

bool pci_msi_enable(struct pci_msi *msi)
{
  return set_enabled(msi, true);
}

bool pci_msi_disable(struct pci_msi *msi)
{
  return set_enabled(msi, false);
}
