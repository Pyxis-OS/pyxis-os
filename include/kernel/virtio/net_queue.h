#ifndef KERNEL_VIRTIO_NET_QUEUE_H
#define KERNEL_VIRTIO_NET_QUEUE_H

#include <kernel/mm/types.h>

#define VIRTIO_NET_QUEUE_SIZE 16
#define VIRTIO_NET_BUFFER_BYTES 2048

struct virtio_net_ring;

/* One direct descriptor per packet, with permanently assigned coherent storage.
 * CPU and DMA addresses are distinct. Only the BSP network worker accesses
 * rings/buffers after boot; IRQ entry only wakes that worker. */
struct virtio_net_queue {
  uintptr_t storage, notify_address;
  phys_addr_t physical;
  volatile struct virtio_net_ring *ring;
  uint16_t index, available, used, outstanding;
  bool receive, device_owned[VIRTIO_NET_QUEUE_SIZE];
};

struct virtio_net_completion {
  unsigned id;
  size_t length;
};

/* BSP/IF=0 before AP startup. Release only unpublished storage or after confirmed
 * reset. Runtime storage and mappings survive until reboot, even after reset. */
enum mm_result virtio_net_queue_allocate(struct virtio_net_queue *queue,
    unsigned index, unsigned maximum, uintptr_t notify_address, bool receive);
void virtio_net_queue_release(struct virtio_net_queue *queue);
phys_addr_t virtio_net_queue_descriptors(const struct virtio_net_queue *queue);
phys_addr_t virtio_net_queue_available(const struct virtio_net_queue *queue);
phys_addr_t virtio_net_queue_used(const struct virtio_net_queue *queue);

/* Borrow a CPU-owned buffer until post. The caller initializes TX bytes first;
 * post lends the buffer to the device until checked completion or reset. */
void *virtio_net_queue_buffer(const struct virtio_net_queue *queue, unsigned id);
void virtio_net_queue_post(struct virtio_net_queue *queue, unsigned id, size_t bytes);
/* Only after DRIVER_OK; publish before checking notification suppression. */
void virtio_net_queue_notify(const struct virtio_net_queue *queue);

/* Snapshot at most 16 completions. Validate the entire batch, including duplicate
 * IDs, before returning ownership. False means broken; caller must stop the NIC.
 * True with count=0 means pending. TX completion length must be zero. */
bool virtio_net_queue_complete(struct virtio_net_queue *queue,
    struct virtio_net_completion completed[VIRTIO_NET_QUEUE_SIZE], unsigned *count);

#endif
