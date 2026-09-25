#ifndef KERNEL_VIRTIO_QUEUE_H
#define KERNEL_VIRTIO_QUEUE_H

#include <kernel/mm/types.h>

#define VIRTQUEUE_DESCRIPTORS 16
#define VIRTQUEUE_REQUEST_BYTES 4096
#define VIRTQUEUE_REPLY_BYTES 8192

struct virtqueue_ring;

/* One direct descriptor chain in flight per queue. Kernel-owned, coherent DMA
 * storage has distinct CPU and device addresses; no user pointers or IOMMU.
 * The sole BSP worker owns submission/completion, with interrupts enabled.
 * IRQ handlers never touch the rings or buffers. */
struct virtqueue {
  uintptr_t storage;
  phys_addr_t physical;
  volatile struct virtqueue_ring *ring;
  void *request, *reply;
  uintptr_t notify_address;
  uint16_t index, size, available, used;
  uint32_t reply_capacity;
  bool in_flight;
};

enum virtqueue_result {
  VIRTQUEUE_PENDING,
  VIRTQUEUE_COMPLETE,
  VIRTQUEUE_BROKEN,
};

/* BSP/IF=0 before AP startup. Allocation failure leaves an empty queue.
 * Release is only for unpublished storage, or after confirmed device reset,
 * before AP startup. Runtime failures retain storage/mappings until reboot. */
enum mm_result virtqueue_allocate(struct virtqueue *queue, unsigned index,
    unsigned maximum, uintptr_t notify_address);
void virtqueue_release(struct virtqueue *queue);
phys_addr_t virtqueue_descriptor_address(const struct virtqueue *queue);
phys_addr_t virtqueue_available_address(const struct virtqueue *queue);
phys_addr_t virtqueue_used_address(const struct virtqueue *queue);

/* Fill request first. Successful submission lends both buffers to the device
 * until a checked completion or confirmed reset. A zero reply size supports
 * requests without a reply; a zero request size supports writable-only buffers. No buffer access/reuse while in_flight is set. */
bool virtqueue_submit(struct virtqueue *queue, size_t request_bytes, size_t reply_bytes);
enum virtqueue_result virtqueue_complete(struct virtqueue *queue, size_t *reply_bytes);

#endif
