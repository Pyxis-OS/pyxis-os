#include <arch/dma.h>
#include <kernel/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/net/panic_tx.h>
#include <kernel/virtio/net_queue.h>

#define NET_DESCRIPTOR_WRITE 2u
#define NET_USED_NO_NOTIFY 1u
#define VIRTIO_NET_STORAGE_BYTES (PAGE_SIZE + VIRTIO_NET_QUEUE_SIZE * VIRTIO_NET_BUFFER_BYTES)

struct net_descriptor {
  uint64_t address;
  uint32_t length;
  uint16_t flags, next;
};

struct net_used_element {
  uint32_t id, length;
};

/* The three ring bases are independent in modern PCI. EVENT_IDX is not
 * negotiated; the trailing event fields are reserved and never read. */
struct virtio_net_ring {
  struct net_descriptor descriptors[VIRTIO_NET_QUEUE_SIZE];
  struct {
    uint16_t flags, index, ring[VIRTIO_NET_QUEUE_SIZE], used_event;
  } available;
  struct {
    uint16_t flags, index;
    struct net_used_element ring[VIRTIO_NET_QUEUE_SIZE];
    uint16_t available_event;
  } used;
};

_Static_assert(sizeof(struct net_descriptor) == 16, "split descriptor layout");
_Static_assert(sizeof(struct net_used_element) == 8, "used element layout");
_Static_assert(offsetof(struct virtio_net_ring, available) % 2 == 0 &&
               offsetof(struct virtio_net_ring, used) % 4 == 0 &&
               sizeof(struct virtio_net_ring) <= PAGE_SIZE, "network ring extent");
_Static_assert(VIRTIO_NET_STORAGE_BYTES % PAGE_SIZE == 0, "network storage pages");

phys_addr_t virtio_net_queue_descriptors(const struct virtio_net_queue *queue)
{
  return queue->physical + offsetof(struct virtio_net_ring, descriptors);
}

phys_addr_t virtio_net_queue_available(const struct virtio_net_queue *queue)
{
  return queue->physical + offsetof(struct virtio_net_ring, available);
}

phys_addr_t virtio_net_queue_used(const struct virtio_net_queue *queue)
{
  return queue->physical + offsetof(struct virtio_net_ring, used);
}

enum mm_result virtio_net_queue_allocate(struct virtio_net_queue *queue, unsigned index,
    unsigned maximum, uintptr_t notify_address, bool receive)
{
  KASSERT(!queue->storage && !queue->physical);
  if (maximum < VIRTIO_NET_QUEUE_SIZE || (maximum & (maximum - 1)) || index > UINT16_MAX) {
    return MM_INVALID;
  }

  uintptr_t storage;
  enum mm_result result = vm_reserve(vm_kernel_space(), VIRTIO_NET_STORAGE_BYTES,
                                     PAGE_SIZE, &storage);
  if (result != MM_OK) {
    return result;
  }
  phys_addr_t physical = pmm_alloc(VIRTIO_NET_STORAGE_BYTES / PAGE_SIZE);
  if (!physical) {
    KASSERT(vm_release(vm_kernel_space(), storage, VIRTIO_NET_STORAGE_BYTES) == MM_OK);
    return MM_NO_MEMORY;
  }

  size_t mapped = 0;
  while (mapped < VIRTIO_NET_STORAGE_BYTES) {
    result = vm_map(vm_kernel_space(), storage + mapped, physical + mapped, PAGE_WRITE);
    if (result != MM_OK) {
      break;
    }
    mapped += PAGE_SIZE;
  }
  if (result != MM_OK) {
    while (mapped) {
      mapped -= PAGE_SIZE;
      phys_addr_t frame;
      KASSERT(vm_unmap(vm_kernel_space(), storage + mapped, &frame) == MM_OK);
    }
    pmm_free(physical, VIRTIO_NET_STORAGE_BYTES / PAGE_SIZE);
    KASSERT(vm_release(vm_kernel_space(), storage, VIRTIO_NET_STORAGE_BYTES) == MM_OK);
    return result;
  }

  memset((void *)storage, 0, VIRTIO_NET_STORAGE_BYTES);
  *queue = (struct virtio_net_queue){
    .storage = storage,
    .physical = physical,
    .ring = (volatile struct virtio_net_ring *)storage,
    .notify_address = notify_address,
    .index = index,
    .receive = receive,
  };
  for (unsigned id = 0; id < VIRTIO_NET_QUEUE_SIZE; ++id) {
    queue->ring->descriptors[id] = (struct net_descriptor){
      .address = physical + PAGE_SIZE + id * VIRTIO_NET_BUFFER_BYTES,
      .flags = receive ? NET_DESCRIPTOR_WRITE : 0,
    };
  }
  return MM_OK;
}

void virtio_net_queue_release(struct virtio_net_queue *queue)
{
  if (!queue->storage) {
    return;
  }
  for (size_t offset = 0; offset < VIRTIO_NET_STORAGE_BYTES; offset += PAGE_SIZE) {
    phys_addr_t frame;
    KASSERT(vm_unmap(vm_kernel_space(), queue->storage + offset, &frame) == MM_OK);
    KASSERT(frame == queue->physical + offset);
  }
  pmm_free(queue->physical, VIRTIO_NET_STORAGE_BYTES / PAGE_SIZE);
  KASSERT(vm_release(vm_kernel_space(), queue->storage, VIRTIO_NET_STORAGE_BYTES) == MM_OK);
  *queue = (struct virtio_net_queue){0};
}

void *virtio_net_queue_buffer(const struct virtio_net_queue *queue, unsigned id)
{
  KASSERT(id < VIRTIO_NET_QUEUE_SIZE && !queue->device_owned[id]);
  return (void *)(queue->storage + PAGE_SIZE + id * VIRTIO_NET_BUFFER_BYTES);
}

void virtio_net_queue_post(struct virtio_net_queue *queue, unsigned id, size_t bytes)
{
  KASSERT(id < VIRTIO_NET_QUEUE_SIZE && !queue->device_owned[id]);
  KASSERT(queue->receive || !queue->panic_reserved || id != VIRTIO_NET_PANIC_DESCRIPTOR);
  KASSERT(bytes && bytes <= VIRTIO_NET_BUFFER_BYTES);
  KASSERT(queue->outstanding < VIRTIO_NET_QUEUE_SIZE);
  queue->ring->descriptors[id].length = bytes;
  queue->device_owned[id] = true;
  ++queue->outstanding;
  queue->ring->available.ring[queue->available % VIRTIO_NET_QUEUE_SIZE] = id;
  /* The available index transfers ownership of the initialized descriptor and
   * buffer. Notification alone is not the ownership boundary. */
  dma_write_barrier();
  queue->ring->available.index = ++queue->available;
}

void virtio_net_queue_notify(const struct virtio_net_queue *queue)
{
  dma_full_barrier();
  if (!(queue->ring->used.flags & NET_USED_NO_NOTIFY)) {
    *(volatile uint16_t *)queue->notify_address = queue->index;
  }
}

bool virtio_net_queue_complete(struct virtio_net_queue *queue,
    struct virtio_net_completion completed[VIRTIO_NET_QUEUE_SIZE], unsigned *count)
{
  *count = 0;
  uint16_t used = queue->ring->used.index;
  unsigned pending = (uint16_t)(used - queue->used);
  if (pending > queue->outstanding) {
    return false;
  }
  dma_read_barrier();
  bool seen[VIRTIO_NET_QUEUE_SIZE] = {0};
  for (unsigned i = 0; i < pending; ++i) {
    unsigned slot = (uint16_t)(queue->used + i) % VIRTIO_NET_QUEUE_SIZE;
    uint32_t id = queue->ring->used.ring[slot].id;
    uint32_t length = queue->ring->used.ring[slot].length;
    if (id >= VIRTIO_NET_QUEUE_SIZE || !queue->device_owned[id] || seen[id] ||
        length > (queue->receive ? VIRTIO_NET_BUFFER_BYTES : 0)) {
      return false;
    }
    seen[id] = true;
    completed[i] = (struct virtio_net_completion){.id = id, .length = length};
  }

  /* Validate before recycling any descriptor: a duplicate in this batch must
   * not be mistaken for completion of a newly reposted receive buffer. */
  for (unsigned i = 0; i < pending; ++i) {
    queue->device_owned[completed[i].id] = false;
  }
  queue->outstanding -= pending;
  queue->used = used;
  *count = pending;
  return true;
}

void *virtio_net_queue_panic_buffer(const struct virtio_net_queue *queue)
{
  return (void *)(queue->storage + PAGE_SIZE +
      VIRTIO_NET_PANIC_DESCRIPTOR * VIRTIO_NET_BUFFER_BYTES);
}

bool virtio_net_queue_panic_begin(struct virtio_net_queue *queue)
{
  if (!queue->panic_reserved || queue->receive || !queue->storage) {
    return false;
  }
  /* Forget normal completion metadata, which may be interrupted mid-update.
   * Only completions after this snapshot can return the fatal buffer. */
  queue->panic_used = queue->ring->used.index;
  dma_read_barrier();
  queue->panic_seen = 0;
  queue->panic_ready = true;
  return true;
}

bool virtio_net_queue_panic_transmit(struct virtio_net_queue *queue, size_t bytes)
{
  if (!queue->panic_ready || queue->panic_failed || !bytes ||
      bytes > VIRTIO_NET_BUFFER_BYTES) {
    return false;
  }
  unsigned id = VIRTIO_NET_PANIC_DESCRIPTOR;
  queue->ring->descriptors[id].length = bytes;
  /* A normal post can be interrupted after incrementing its software index.
   * Only the actual DMA index establishes the next free publication slot. */
  uint16_t available = queue->ring->available.index;
  queue->ring->available.ring[available % VIRTIO_NET_QUEUE_SIZE] = id;
  dma_write_barrier();
  queue->ring->available.index = (uint16_t)(available + 1);
  virtio_net_queue_notify(queue);

  for (unsigned poll = 0; poll < NET_PANIC_COMPLETION_POLLS; ++poll) {
    uint16_t used = queue->ring->used.index;
    unsigned pending = (uint16_t)(used - queue->panic_used);
    if (pending > VIRTIO_NET_QUEUE_SIZE) {
      break;
    }
    dma_read_barrier();
    uint16_t seen = queue->panic_seen;
    bool completed = false;
    for (unsigned i = 0; i < pending; ++i) {
      unsigned slot = (uint16_t)(queue->panic_used + i) % VIRTIO_NET_QUEUE_SIZE;
      uint32_t returned = queue->ring->used.ring[slot].id;
      uint32_t length = queue->ring->used.ring[slot].length;
      if (returned >= VIRTIO_NET_QUEUE_SIZE || length || (seen & (1u << returned))) {
        queue->panic_failed = true;
        return false;
      }
      seen |= 1u << returned;
      completed |= returned == id;
    }
    queue->panic_used = used;
    queue->panic_seen = seen;
    if (completed) {
      /* Ordinary descriptors are never reused after takeover. The reserved
       * descriptor becomes eligible only after this whole batch validates. */
      queue->panic_seen &= ~(1u << id);
      return true;
    }
    __asm__ volatile("pause");
  }
  queue->panic_failed = true;
  return false;
}
