#ifndef KERNEL_VIRTIO_PCI_H
#define KERNEL_VIRTIO_PCI_H
#include <kernel/boot.h>

/* Prepare the first modern filesystem function, if present, before AP startup.
 * Own resources, negotiate the modern baseline and inspect filesystem queues.
 * Leaves DRIVER_OK clear, queues disabled and PCI DMA/interrupts disabled. */
void virtio_fs_pci_prepare(const struct boot_info *boot);
#endif
