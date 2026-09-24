#ifndef KERNEL_VIRTIO_PCI_H
#define KERNEL_VIRTIO_PCI_H
#include <kernel/boot.h>

/* BSP/IF=0 before AP startup. Own resources, negotiate features, prepare masked
 * MSI-X routing and allocate/program both queues. DRIVER_OK and DMA stay clear.
 * Boot failure unwinds only after confirmed reset; otherwise resources remain. */
void virtio_fs_pci_prepare(const struct boot_info *boot);

/* Once after task_init(), BSP/IF=0. Creates the worker if preparation succeeded.
 * That worker activates DMA/delivery and starts the FUSE session with IF=1. */
void virtio_fs_pci_start(void);

/* BSP interrupt entry, IF=0. Records activity and wakes the sole worker;
 * arch acknowledges the APIC. No ISR-register read is needed with MSI-X. */
void virtio_fs_pci_interrupt(void);

/* Sole BSP worker, IF=1, no held locks. Copies into owned DMA storage and sleeps
 * for completion, bounded by five seconds. Reply length is checked against the
 * caller's capacity before copying. Returns NULL or a diagnostic; any failure
 * ends the session and requires the worker's stop/reset path before returning.
 * No concurrent callers, allocation, user pointers or request cancellation. */
const char *virtio_fs_pci_request(const void *request, size_t request_bytes,
    void *reply, size_t reply_capacity, size_t *reply_bytes);
#endif
