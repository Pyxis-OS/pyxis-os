#ifndef KERNEL_VIRTIO_PCI_H
#define KERNEL_VIRTIO_PCI_H
#include <kernel/boot.h>

/* Prepare the first modern filesystem function, if present, before AP startup.
 * Own resources, negotiate the modern baseline and inspect filesystem queues.
 * Programs BSP MSI-X routing under both function and entry masks. Leaves
 * DRIVER_OK clear, queues disabled and PCI DMA/interrupt delivery disabled. */
void virtio_fs_pci_prepare(const struct boot_info *boot);

/* BSP interrupt entry, IF=0. Records activity and wakes the sole worker;
 * arch acknowledges the APIC. No ISR-register read is needed with MSI-X. */
void virtio_fs_pci_interrupt(void);

/* Sole BSP kernel worker, IF=1, no held locks, after successful preparation.
 * Consumes remembered interrupt activity or parks until it arrives. Returns
 * with IF=1; recheck all queue/configuration state, not an interrupt count.
 * The wait pointer is detached before return. Delivery stays masked until
 * queue/worker activation is implemented. */
void virtio_fs_pci_wait_interrupt(void);
#endif
