#ifndef KERNEL_DISPLAY_BOCHS_H
#define KERNEL_DISPLAY_BOCHS_H

#include <kernel/boot.h>
#include <kernel/fb/fb.h>
#include <kernel/pci.h>

bool bochs_display_matches(const struct pci_device *device);

/* BSP/IF=0 before AP startup. NULL preserves verified firmware scanout.
 * An unverifiable restoration is fatal. The direct target and PCI resources
 * remain owned until reboot; presentation uses ordinary framebuffer copies. */
const struct framebuffer *bochs_display_prepare(const struct boot_info *boot,
    struct pci_device *device, const char *size);

#endif
