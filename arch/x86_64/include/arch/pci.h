#ifndef ARCH_PCI_H
#define ARCH_PCI_H

#include <kernel/mm/types.h>
#include <kernel/pci.h>

#define PCI_ECAM_BUS_BYTES (UINT64_C(1) << 20)

/* The initial platform supports one segment-zero MCFG range starting at bus 0. */
struct pci_ecam {
  phys_addr_t physical;
  unsigned bus_count;
};

/* Paging publishes the permanently mapped aperture after the CR3 switch.
 * Zero buses means unavailable. No firmware or temporary mapping is retained. */
void arch_pci_init(unsigned bus_count);
unsigned arch_pci_bus_count(void);

/* Naturally aligned accesses within an available function's 4 KiB config page.
 * The mapping is supervisor-only, read-only, NX and uncached. */
uint8_t pci_read8(struct pci_address address, unsigned offset);
uint16_t pci_read16(struct pci_address address, unsigned offset);
uint32_t pci_read32(struct pci_address address, unsigned offset);

#endif
