#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/panic.h>
#include <kernel/pci/msi.h>
#include <kernel/pci/registers.h>

#define MSI_MESSAGE_CAPABLE_MASK (7u << 1)
#define MSI_MESSAGE_ENABLE_MASK (7u << 4)
#define MSI_64BIT (1u << 7)
#define MSI_VECTOR_MASK_CAPABLE (1u << 8)
#define MSI_LAYOUT_MASK (MSI_MESSAGE_CAPABLE_MASK | MSI_64BIT | MSI_VECTOR_MASK_CAPABLE)
#define MSI_ADDRESS_LOW 4u
#define MSI_ADDRESS_HIGH 8u
#define MSI_DATA_32 8u
#define MSI_MASK_32 12u
#define MSI_PENDING_32 16u
#define MSI_DATA_64 12u
#define MSI_MASK_64 16u
#define MSI_PENDING_64 20u
#define MSI_MAX_MESSAGE_ORDER 5u

static void require_claim(const struct pci_claim *claim)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(claim && claim->device && !claim->reserved && claim->device->owner == claim);
}

static bool intx_disabled(const struct pci_claim *claim)
{
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  return command != UINT16_MAX && (command & PCI_COMMAND_INTX_DISABLE);
}

static uint32_t message_mask(uint16_t layout)
{
  unsigned messages = 1u << ((layout & MSI_MESSAGE_CAPABLE_MASK) >> 1);
  return messages == 32 ? UINT32_MAX : (UINT32_C(1) << messages) - 1;
}

static bool control_matches(const struct pci_msi *msi, bool enabled)
{
  uint16_t control = pci_read16(msi->claim->device->address,
      msi->capability + PCI_MSI_CONTROL);
  unsigned mask = MSI_LAYOUT_MASK | MSI_MESSAGE_ENABLE_MASK | PCI_MSI_ENABLE;
  return control != UINT16_MAX && (control & mask) ==
      (msi->layout | (enabled ? PCI_MSI_ENABLE : 0));
}

static bool set_mask(const struct pci_msi *msi, uint32_t masked)
{
  if (!(msi->layout & MSI_VECTOR_MASK_CAPABLE)) {
    return true;
  }
  unsigned offset = msi->capability +
    (msi->layout & MSI_64BIT ? MSI_MASK_64 : MSI_MASK_32);
  uint32_t bits = message_mask(msi->layout);
  struct pci_address address = msi->claim->device->address;
  uint32_t value = pci_read32(address, offset);
  pci_write32(msi->claim, offset, (value & ~bits) | (masked & bits));
  return (pci_read32(address, offset) & bits) == (masked & bits);
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
    if (capability || !pci_capability_fits(claim, offset, PCI_MSI_CONTROL + sizeof(uint16_t))) {
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
  unsigned order = (control & MSI_MESSAGE_CAPABLE_MASK) >> 1;
  if (control == UINT16_MAX || (control & PCI_MSI_ENABLE) || order > MSI_MAX_MESSAGE_ORDER) {
    return false;
  }
  bool wide = control & MSI_64BIT;
  unsigned data = wide ? MSI_DATA_64 : MSI_DATA_32;
  size_t bytes = control & MSI_VECTOR_MASK_CAPABLE ?
    (wide ? MSI_PENDING_64 : MSI_PENDING_32) + sizeof(uint32_t) : data + sizeof(uint16_t);
  if (!pci_capability_fits(claim, capability, bytes)) {
    return false;
  }
  struct apic_msi_message message = apic_bsp_msi_message(vector);
  if (!wide && message.address_high) {
    return false;
  }
  struct pci_msi prepared = {
    .claim = claim, .capability = capability, .layout = control & MSI_LAYOUT_MASK,
  };
  /* Both delivery gates stay closed while programming. Pending Bits is read-only;
   * admitting its span does not authorize a write to acknowledge interrupts. */
  pci_write16(claim, control_offset, control & ~MSI_MESSAGE_ENABLE_MASK);
  if (!control_matches(&prepared, false) || !set_mask(&prepared, UINT32_MAX)) {
    return false;
  }
  pci_write32(claim, capability + MSI_ADDRESS_LOW, message.address_low);
  if (wide) {
    pci_write32(claim, capability + MSI_ADDRESS_HIGH, message.address_high);
  }
  pci_write16(claim, capability + data, message.data);
  if (pci_read32(address, capability + MSI_ADDRESS_LOW) != message.address_low ||
      (wide && pci_read32(address, capability + MSI_ADDRESS_HIGH) != message.address_high) ||
      pci_read16(address, capability + data) != message.data ||
      !control_matches(&prepared, false) || !intx_disabled(claim)) {
    return false;
  }
  *msi = prepared;
  return true;
}

static bool set_enabled(struct pci_msi *msi, bool enabled)
{
  KASSERT(msi && msi->claim && msi->capability);
  require_claim(msi->claim);
  unsigned offset = msi->capability + PCI_MSI_CONTROL;
  struct pci_address address = msi->claim->device->address;
  uint16_t control = pci_read16(address, offset);
  /* Always attempt global disable, including when a mask/layout readback fails.
   * Unmasking message zero happens only after delivery-off is confirmed. */
  pci_write16(msi->claim, offset, control & ~(PCI_MSI_ENABLE | MSI_MESSAGE_ENABLE_MASK));
  control = pci_read16(address, offset);
  bool same_layout = control != UINT16_MAX && (control & MSI_LAYOUT_MASK) == msi->layout;
  bool disabled = same_layout && !(control & (PCI_MSI_ENABLE | MSI_MESSAGE_ENABLE_MASK));
  bool masked = same_layout && set_mask(msi, UINT32_MAX);
  if (!disabled || !masked || !intx_disabled(msi->claim)) {
    return false;
  }
  if (!enabled) {
    return true;
  }
  if (!set_mask(msi, UINT32_MAX & ~UINT32_C(1))) {
    return false;
  }
  pci_write16(msi->claim, offset,
      (control & ~MSI_MESSAGE_ENABLE_MASK) | PCI_MSI_ENABLE);
  return control_matches(msi, true) && intx_disabled(msi->claim);
}

bool pci_msi_enable(struct pci_msi *msi)
{
  return set_enabled(msi, true);
}

bool pci_msi_disable(struct pci_msi *msi)
{
  return set_enabled(msi, false);
}
