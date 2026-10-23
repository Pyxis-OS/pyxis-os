#ifndef KERNEL_PCI_MSI_H
#define KERNEL_PCI_MSI_H

#include <kernel/pci.h>

struct pci_msi {
  struct pci_claim *claim;
  unsigned capability;
  uint16_t layout;
};

/* Completed claim, BSP/IF=0 before DMA. Checked 32/64-bit MSI, with optional
 * per-vector masks; exactly one message is enabled, even when more are supported.
 * Prepare binds the BSP vector with delivery off and supported messages masked;
 * enable unmasks only message zero. Pending Bits is never written.
 * Runtime enable/disable require the same owner and IF=0, keeping INTx off. */
bool pci_msi_prepare(struct pci_claim *claim, struct pci_msi *msi, uint8_t vector);
bool pci_msi_enable(struct pci_msi *msi);
bool pci_msi_disable(struct pci_msi *msi);

#endif
