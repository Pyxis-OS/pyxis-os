#ifndef KERNEL_VIRTIO_BLK_H
#define KERNEL_VIRTIO_BLK_H

#include <kernel/block.h>
#include <kernel/boot.h>

/* BSP/IF=0. Inventory all VirtIO block candidates before AP startup; start one
 * worker per prepared modern device after task_init(). Failed/unsupported
 * entries stay in the inventory and do not disable other devices. */
void virtio_blk_prepare(const struct boot_info *boot);
void virtio_blk_start(void);

/* BSP/IF=0 after preparation. Initial IDs are local index+1 values. Assign
 * registry IDs before AP or worker startup; IDs are immutable afterward.
 * Incomplete PCI inventory is reported separately from each known candidate's
 * preparation result. */
size_t virtio_blk_device_count(void);
block_device_id virtio_blk_device_at(size_t index);
void virtio_blk_set_id(size_t index, block_device_id id);
bool virtio_blk_inventory_complete(void);
enum block_preparation virtio_blk_preparation_result(block_device_id device);

/* Backend entry points follow the public block request and ticket contract. */
enum block_result virtio_blk_get_info(block_device_id device, struct block_info *info);
enum block_result virtio_blk_submit(block_device_id device, enum block_operation operation,
    uint64_t first_block, uint32_t block_count, const void *write_bytes,
    struct block_ticket *ticket);
enum block_result virtio_blk_collect(const struct block_ticket *ticket, void *read_bytes,
    size_t read_capacity, struct block_completion *completion);
enum block_result virtio_blk_abandon(const struct block_ticket *ticket);
enum block_result virtio_blk_wait(const struct block_ticket *ticket, uint64_t deadline_ns);

/* BSP interrupt entry, IF=0: wake interrupt-ready workers sharing this vector
 * without touching their DMA buffers. Spurious wakeups are harmless. */
void virtio_blk_interrupt(void);

#endif
