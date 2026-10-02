#ifndef KERNEL_USB_XHCI_H
#define KERNEL_USB_XHCI_H

#include <kernel/boot.h>

/* One unique class-matched controller. Preparation allocates/maps before AP
 * startup; activation creates a BSP worker after task initialization. Storage
 * selection and USB descriptor/class handling are separate consumers. */
void xhci_prepare(const struct boot_info *boot);
void xhci_start(void);
void xhci_interrupt(void);

#endif
