#ifndef KERNEL_VIRTIO_PCI_H
#define KERNEL_VIRTIO_PCI_H
#include <kernel/boot.h>
#include <kernel/virtio/fs.h>

/* BSP/IF=0 before AP startup. Own resources, negotiate features, prepare masked
 * MSI-X routing and allocate/program both queues. DRIVER_OK and DMA stay clear.
 * Boot failure unwinds only after confirmed reset; otherwise resources remain. */
void virtio_fs_pci_prepare(const struct boot_info *boot);
/* Immutable after prepare, including devices whose preparation failed. */
bool virtio_fs_pci_present(void);

/* Once after task_init(), BSP/IF=0. Creates the worker if preparation succeeded.
 * That worker activates DMA/delivery and starts the FUSE session with IF=1. */
void virtio_fs_pci_start(void);

/* BSP interrupt entry, IF=0. Records activity and wakes the sole worker;
 * arch acknowledges the APIC. No ISR-register read is needed with MSI-X. */
void virtio_fs_pci_interrupt(void);
/* BSP/IF=0: native work and deferred cleanup share the worker wake handoff. */
void virtio_fs_pci_wake(void);

struct virtio_fs_timing {
  uint64_t submitted_ns, ended_ns;
  bool completed;
};

/* Sole BSP worker, IF=1, no held locks. Copies into owned DMA storage and sleeps
 * for completion, bounded by five seconds. Transport/protocol failure stops
 * the session before returning; storage remains mapped until reboot.
 * *submitted records whether the descriptor chain was published, even on
 * failure. Optional timing ends at observed completion/failure before recovery;
 * meaningful only when submitted. No added clocks when NULL. No concurrent
 * callers, allocation, user pointers or cancellation. */
enum virtio_fs_result virtio_fs_pci_request(const void *request, size_t request_bytes,
    void *reply, size_t reply_capacity, size_t *reply_bytes, bool *submitted,
    struct virtio_fs_timing *timing);
/* High-priority FORGET with no device-writable payload; waits for used-ring
 * completion, not a FUSE reply. Serialized with ordinary requests. */
enum virtio_fs_result virtio_fs_pci_forget(const void *request, size_t request_bytes);
/* Same worker context, no published waiter. Idempotent after stopping. */
void virtio_fs_pci_stop(const char *reason);
#endif
