#ifndef USB_BLOCK_H
#define USB_BLOCK_H

#include <kernel/block.h>

struct usb_host_controller;
struct usb_bot;
struct usb_block_pool;
struct usb_block_device;

/* BSP/IF=0 before AP startup. Metadata covers every inspected candidate;
 * captured I/O buffers cover the bounded supported-storage admission pool. */
struct usb_block_pool *usb_block_prepare(struct usb_host_controller *host, size_t candidate_capacity);
void usb_block_release_prepared(struct usb_block_pool *pool);
/* BSP/IF=0 after the owning controller worker's terminal media setup. No allocation. */
struct usb_block_device *usb_block_bind(struct usb_block_pool *pool, struct usb_bot *bot);
/* BSP/IF=0. Setup results and assigned nonzero IDs remain immutable. */
enum block_preparation usb_block_preparation(const struct usb_block_device *device);
void usb_block_set_id(struct usb_block_device *device, block_device_id id);

/* These obey the ordinary block ticket, context and pointer-lifetime contract. */
enum block_result usb_block_get_info(struct usb_block_device *device, struct block_info *info);
enum block_result usb_block_submit(struct usb_block_device *device, enum block_operation operation,
    uint64_t first_block, uint32_t block_count, const void *write_bytes, struct block_ticket *ticket);
enum block_result usb_block_collect(struct usb_block_device *device, const struct block_ticket *ticket,
    void *read_bytes, size_t read_capacity, struct block_completion *completion);
enum block_result usb_block_abandon(struct usb_block_device *device, const struct block_ticket *ticket);
enum block_result usb_block_wait(struct usb_block_device *device, const struct block_ticket *ticket,
    uint64_t deadline_ns);
/* Owning controller worker, IF=1. Serial BOT exchanges, FIFO per device. */
void usb_block_process(struct usb_block_pool *pool);
/* BSP/IF=0. Active captured buffers remain owned until process returns. */
void usb_block_fail(struct usb_block_pool *pool);

#endif
