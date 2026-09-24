#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <stdint.h>

#define PCI_BUS_COUNT 256
#define PCI_DEVICE_COUNT 32
#define PCI_FUNCTION_COUNT 8
#define PCI_CONFIG_BYTES 4096

struct pci_address {
  uint8_t bus, device, function;
};

/* BSP boot inventory. Reads firmware configuration without activating devices,
 * sizing BARs or changing bus assignments. No persistent device registry yet. */
void pci_discover(void);

#endif
