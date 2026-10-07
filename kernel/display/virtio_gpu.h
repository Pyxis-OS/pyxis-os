#ifndef KERNEL_DISPLAY_VIRTIO_GPU_H
#define KERNEL_DISPLAY_VIRTIO_GPU_H

#include <kernel/boot.h>
#include <kernel/fb/fb.h>

/* BSP/IF=0 before AP startup. Without a boot framebuffer, a bounded temporary
 * queue queries the enabled output's size. Selected remains true on failure:
 * a detected GPU cannot promise firmware scanout after transport reset.
 * The RAM layout remains until reboot. */
const struct framebuffer *virtio_gpu_prepare(const struct boot_info *boot, bool *selected);
/* Sole BSP presenter, IF=1. Start creates and attaches the prepared resource;
 * present transfers a complete frame before selecting its scanout and flushing.
 * Failure is permanent and retains all runtime DMA storage until reboot. */
bool virtio_gpu_start(void);
bool virtio_gpu_present(void);
/* The display owner bounds and gates copies, with no command in flight. */
void virtio_gpu_copy(size_t offset, const void *pixels, size_t bytes);
/* BSP interrupt entry, IF=0: records activity and detaches/wakes the waiter. */
void virtio_gpu_interrupt(void);

#endif
