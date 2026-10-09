#ifndef ARCH_AMD_RENOIR_FIRMWARE_H
#define ARCH_AMD_RENOIR_FIRMWARE_H

#include <kernel/pci.h>

/* BSP diagnostics only. Uses numeric metadata copied before paging; no device
 * access, firmware calls or retained ACPI/VBIOS aliases. */
void renoir_inventory_firmware(struct pci_address address);

#endif
