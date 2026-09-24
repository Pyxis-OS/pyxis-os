#ifndef KERNEL_VIRTIO_PCI_H
#define KERNEL_VIRTIO_PCI_H
#include <kernel/boot.h>

/* Prepare the first modern filesystem function, if present, before AP startup.
 * Reset and resource ownership only; no feature negotiation, queues or DMA yet. */
void virtio_fs_pci_prepare(const struct boot_info *boot);
#endif
