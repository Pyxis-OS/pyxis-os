#ifndef ARCH_AMD_RENOIR_INVENTORY_H
#define ARCH_AMD_RENOIR_INVENTORY_H

#include <kernel/boot.h>

/* Opt-in only. Bootstrap captures bounded numeric VFCT/ATOM metadata before
 * replacing the root; no firmware pointers or raw BIOS bytes survive. */
void renoir_inventory_boot(const struct boot_info *boot);
/* BSP, IF=0 before AP startup. Read-only PCI/MMIO inventory, then unmap.
 * No device claim, GPU/PCI/MC write, allocation of scanout memory or flip. */
void renoir_inventory(const struct boot_info *boot);

#endif
