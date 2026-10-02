#ifndef KERNEL_PCI_MSIX_H
#define KERNEL_PCI_MSIX_H

#include <kernel/pci.h>
#include <kernel/pci/registers.h>

struct pci_msix_entry {
  uint32_t address_low, address_high, data, control;
};

_Static_assert(sizeof(struct pci_msix_entry) == PCI_MSIX_ENTRY_BYTES,
               "PCI MSI-X table entry layout");

/* Stable caller-owned storage throughout the claim, including late IRQs after
 * runtime stop. Regions belong to the claim and unwind with its boot release. */
struct pci_msix {
  struct pci_claim *claim;
  unsigned capability, entries;
  struct pci_region table, pba;
};

/* Completed claim, sized BARs, BSP/IF=0 before AP startup. Exactly one disabled
 * MSI-X capability; table/PBA extents must fit and not overlap. Other device
 * register regions must be checked for overlap by their driver before mapping.
 * Discovery failure leaves the initially empty record unchanged. Mapping failure
 * leaves any successfully mapped region owned by the claim for boot unwind. */
bool pci_msix_discover(struct pci_claim *claim, struct pci_msix *msix);
enum mm_result pci_msix_map(struct pci_msix *msix, const struct boot_info *boot);
/* BSP/IF=0. Enable under function mask, mask every entry and route entry zero to
 * the statically owned BSP vector with readback. Driver-specific vector indices
 * and interrupt sources remain the driver's responsibility. */
bool pci_msix_prepare(struct pci_msix *msix, uint8_t vector);
/* Established owner, BSP/IF=0, including runtime. Enable unmasks entry zero,
 * then the function. Disable masks entry zero/function before clearing enable.
 * Neither operation proves device DMA stopped or retracts a sent interrupt. */
bool pci_msix_enable(struct pci_msix *msix);
bool pci_msix_disable(struct pci_msix *msix);

#endif
