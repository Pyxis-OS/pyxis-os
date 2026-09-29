#ifndef KERNEL_VIRTIO_QUEUE_H
#define KERNEL_VIRTIO_QUEUE_H

#include <kernel/mm/dma.h>

#define VIRTQUEUE_MAX_SIZE 32768u

struct virtqueue_descriptor;
struct virtqueue_available;
struct virtqueue_used;
struct virtqueue_entry;
struct virtqueue_used_element;

enum virtqueue_access {
  VIRTQUEUE_DEVICE_READ,
  VIRTQUEUE_DEVICE_WRITE,
};

struct virtqueue_segment {
  phys_addr_t physical;
  size_t bytes;
  enum virtqueue_access access;
};

struct virtqueue_completion {
  uint64_t request_id;
  uint32_t written;
};

/* Sole BSP worker owns rings/bookkeeping; IRQ handlers only wake it. Payload
 * allocations belong to drivers. Never modify these fields from a driver. */
struct virtqueue {
  struct dma_buffer storage;
  volatile struct virtqueue_descriptor *descriptors;
  volatile struct virtqueue_available *available_ring;
  volatile struct virtqueue_used *used_ring;
  struct virtqueue_entry *entries;
  struct virtqueue_used_element *snapshot;
  uintptr_t notify_address;
  size_t available_offset, used_offset;
  uint16_t index, size, available, used, free_head, free_count, outstanding;
  bool stopped;
};

enum virtqueue_submit_result {
  VIRTQUEUE_ACCEPTED,
  VIRTQUEUE_FULL,
  VIRTQUEUE_SUBMIT_INVALID,
  VIRTQUEUE_SUBMIT_STOPPED,
};

enum virtqueue_result {
  VIRTQUEUE_PENDING,
  VIRTQUEUE_COMPLETE,
  VIRTQUEUE_BROKEN,
  VIRTQUEUE_INVALID,
  VIRTQUEUE_STOPPED,
};

/* BSP/IF=0 before AP startup. size is the driver's selected power of two,
 * bounded by maximum and VIRTQUEUE_MAX_SIZE. Failure leaves an empty queue.
 * Release only unpublished storage or after confirmed reset, before AP startup.
 * Runtime failure retains storage and mappings until reboot. */
enum mm_result virtqueue_allocate(struct virtqueue *queue, unsigned index,
    unsigned size, unsigned maximum, uintptr_t notify_address);
void virtqueue_release(struct virtqueue *queue);
phys_addr_t virtqueue_descriptor_address(const struct virtqueue *queue);
phys_addr_t virtqueue_available_address(const struct virtqueue *queue);
phys_addr_t virtqueue_used_address(const struct virtqueue *queue);

/* IF=1, sole BSP worker. Copies segment descriptions, not payloads. Device-read
 * segments precede device-write segments; total chain bytes fit uint32_t.
 * The driver supplies valid DMA extents
 * without conflicting reuse and an ID unique among outstanding requests.
 * Only ACCEPTED lends buffers to the device. The segment array may then expire;
 * payloads remain device-owned until checked completion or confirmed reset.
 * No allocation, hidden waiting or notification. */
enum virtqueue_submit_result virtqueue_submit(struct virtqueue *queue,
    const struct virtqueue_segment *segments, size_t count, uint64_t request_id);
/* After DRIVER_OK: publish all submissions before notifying. Stopped is a no-op. */
void virtqueue_notify(const struct virtqueue *queue);

/* Validate the entire observed batch before returning any ownership. The caller
 * supplies a non-null count and capacity for its outstanding-request limit.
 * Insufficient capacity
 * returns INVALID without consuming entries. Only COMPLETE makes count entries
 * valid; other results set count=0. Corruption stops the queue and retains all
 * outstanding ownership. No allocation; completions may be out of order. */
enum virtqueue_result virtqueue_complete(struct virtqueue *queue,
    struct virtqueue_completion *completed, size_t capacity, size_t *count);

/* Stop rejects submissions/notifications, retaining ownership. Confirm reset only
 * after the driver observes completed device reset: retire ownership, emit no
 * completions, retain allocations and leave the queue stopped. */
void virtqueue_stop(struct virtqueue *queue);
void virtqueue_confirm_reset(struct virtqueue *queue);

#endif
