#ifndef KERNEL_VIRTIO_BLK_H
#define KERNEL_VIRTIO_BLK_H

#include <kernel/boot.h>

/* BSP/IF=0. Inventory all VirtIO block candidates before AP startup; start one
 * worker per prepared modern device after task_init(). Failed/unsupported
 * entries stay in the inventory and do not disable other devices. */
void virtio_blk_prepare(const struct boot_info *boot);
void virtio_blk_start(void);
/* BSP interrupt entry, IF=0: wake interrupt-ready workers sharing this vector
 * without touching their DMA buffers. Spurious wakeups are harmless. */
void virtio_blk_interrupt(void);

#endif
