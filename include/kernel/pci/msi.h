#ifndef KERNEL_PCI_MSI_H
#define KERNEL_PCI_MSI_H

#include <kernel/pci.h>

struct pci_msi {
  struct pci_claim *claim;
  unsigned capability;
};

/* Completed claim, BSP/IF=0 before DMA. Checked single-message, 64-bit MSI
 * without per-vector masking. Prepare binds the BSP vector with delivery off;
 * runtime enable/disable require the same owner and IF=0, keeping INTx off. */
bool pci_msi_prepare(struct pci_claim *claim, struct pci_msi *msi, uint8_t vector);
bool pci_msi_enable(struct pci_msi *msi);
bool pci_msi_disable(struct pci_msi *msi);

#endif
