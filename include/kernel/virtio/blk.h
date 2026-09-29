#ifndef KERNEL_VIRTIO_BLK_H
#define KERNEL_VIRTIO_BLK_H

#include <kernel/boot.h>

/* BSP/IF=0. Prepare before AP startup; start after task_init(). Missing,
 * ambiguous or unsupported devices leave block operations unavailable. */
void virtio_blk_prepare(const struct boot_info *boot);
void virtio_blk_start(void);
/* BSP interrupt entry, IF=0: wake the worker without touching DMA buffers. */
void virtio_blk_interrupt(void);

#endif
