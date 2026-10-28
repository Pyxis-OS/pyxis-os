#ifndef ARCH_INTEL_DISPLAY_INVENTORY_H
#define ARCH_INTEL_DISPLAY_INVENTORY_H

#include <kernel/boot.h>

/* Opt-in boot-only observation; no PCI or GPU writes, including power requests. */
void intel_display_inventory(const struct boot_info *boot);

#endif
