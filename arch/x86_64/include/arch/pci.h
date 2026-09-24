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
 * The mapping is supervisor-only, NX and uncached; unclaimed pages are RO. */
uint8_t pci_read8(struct pci_address address, unsigned offset);
uint16_t pci_read16(struct pci_address address, unsigned offset);
uint32_t pci_read32(struct pci_address address, unsigned offset);

/* Excludes the existing ECAM/APIC/HPET register mappings. Extent must not overflow. */
bool arch_pci_mmio_available(phys_addr_t physical, size_t bytes);

/* Used by PCI ownership code only, BSP/IF=0. Permission changes precede AP
 * startup; an established owner may write configuration during operation. */
void arch_pci_config_writable(struct pci_address address, bool writable);
void arch_pci_write8(struct pci_address address, unsigned offset, uint8_t value);
void arch_pci_write16(struct pci_address address, unsigned offset, uint16_t value);
void arch_pci_write32(struct pci_address address, unsigned offset, uint32_t value);

#endif
