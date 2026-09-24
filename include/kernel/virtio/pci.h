#ifndef KERNEL_VIRTIO_PCI_H
#define KERNEL_VIRTIO_PCI_H
#include <kernel/boot.h>
#include <kernel/virtio/fs.h>

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
 * for completion, bounded by five seconds. Transport/protocol failure stops
 * the session before returning; storage remains mapped until reboot.
 * No concurrent callers, allocation, user pointers or request cancellation. */
enum virtio_fs_result virtio_fs_pci_request(const void *request, size_t request_bytes,
    void *reply, size_t reply_capacity, size_t *reply_bytes);
/* High-priority FORGET with no device-writable payload; waits for used-ring
 * completion, not a FUSE reply. Serialized with ordinary requests. */
enum virtio_fs_result virtio_fs_pci_forget(const void *request, size_t request_bytes);
/* Same worker context, no published waiter. Idempotent after stopping. */
void virtio_fs_pci_stop(const char *reason);
#endif
