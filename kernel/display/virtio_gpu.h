#ifndef KERNEL_DISPLAY_VIRTIO_GPU_H
#define KERNEL_DISPLAY_VIRTIO_GPU_H

#include <kernel/boot.h>
#include <kernel/fb/fb.h>

struct pci_device;
struct pointer_frame;
bool virtio_gpu_matches(const struct pci_device *device);
/* BSP/IF=0 before AP startup, selected device and early console retired.
 * Without a boot framebuffer, a bounded temporary queue queries geometry.
 * Reset prevents firmware fallback. The descriptor stays at a stable address;
 * successful resize commits replace its geometry and backing. */
const struct framebuffer *virtio_gpu_prepare(const struct boot_info *boot,
    struct pci_device *device);
/* Sole BSP presenter, IF=1. Start creates and attaches the prepared resource;
 * present transfers a complete frame before selecting its scanout and flushing.
 * Failure is permanent and retains all runtime DMA storage until reboot. */
bool virtio_gpu_start(void);
bool virtio_gpu_present(void);
/* Same owner/context, between frames: read transport status and poll any
 * outstanding cursor completion/deadline without waiting or posting commands. */
bool virtio_gpu_service(void);
/* After successful normal presentation, within the same immutable frame lease.
 * Ordinary commands may remain posted in driver-owned storage. Capture drains
 * matching completion, confirming consumption rather than acknowledged display.
 * Failure stops both queues and prevents matching capture publication. */
bool virtio_gpu_pointer_present(const struct pointer_frame *frame, bool capture);
/* Frame copies require the control queue drained; cursor storage is disjoint. */
void virtio_gpu_copy(size_t offset, const void *pixels, size_t bytes);
/* BSP interrupt entry, IF=0: records activity and detaches/wakes the waiter. */
void virtio_gpu_interrupt(void);

/* Same owner and context as presentation; commit alone runs with IF=0.
 * No frame lease may overlap a resize transaction. */
bool virtio_gpu_available(void);
const struct framebuffer *virtio_gpu_resize_prepare(void);
bool virtio_gpu_resize_switch(void);
bool virtio_gpu_resize_cancel(void);
bool virtio_gpu_resize_defer(void);
void virtio_gpu_resize_commit(void);
void virtio_gpu_resize_finish(void);
void virtio_gpu_resize_disable(void);

#endif
