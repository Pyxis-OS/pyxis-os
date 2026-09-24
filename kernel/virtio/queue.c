#include <arch/dma.h>
#include <kernel/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/virtio/queue.h>

#define VIRTQUEUE_DESC_NEXT 1u
#define VIRTQUEUE_DESC_WRITE 2u
#define VIRTQUEUE_USED_NO_NOTIFY 1u
#define VIRTQUEUE_REQUEST_DESCRIPTOR 0
#define VIRTQUEUE_REPLY_DESCRIPTOR 1
#define VIRTQUEUE_REQUEST_OFFSET PAGE_SIZE
#define VIRTQUEUE_REPLY_OFFSET (VIRTQUEUE_REQUEST_OFFSET + VIRTQUEUE_REQUEST_BYTES)
#define VIRTQUEUE_STORAGE_BYTES (VIRTQUEUE_REPLY_OFFSET + VIRTQUEUE_REPLY_BYTES)

struct virtqueue_descriptor {
  uint64_t address;
  uint32_t length;
  uint16_t flags, next;
};

struct virtqueue_used_element {
  uint32_t id, length;
};

/* Ring bases are independent in modern PCI. Fixed-capacity storage also works
 * with smaller negotiated queues: only the first size slots are used. Event
 * index fields are unused because VIRTIO_F_EVENT_IDX is not negotiated. */
struct virtqueue_ring {
  struct virtqueue_descriptor descriptors[VIRTQUEUE_DESCRIPTORS];
  struct {
    uint16_t flags, index, ring[VIRTQUEUE_DESCRIPTORS], used_event;
  } available;
  struct {
    uint16_t flags, index;
    struct virtqueue_used_element ring[VIRTQUEUE_DESCRIPTORS];
    uint16_t available_event;
  } used;
};

_Static_assert(sizeof(struct virtqueue_descriptor) == 16, "split descriptor layout");
_Static_assert(sizeof(struct virtqueue_used_element) == 8, "used element layout");
_Static_assert(offsetof(struct virtqueue_ring, available) % 2 == 0 &&
               offsetof(struct virtqueue_ring, used) % 4 == 0 &&
               sizeof(struct virtqueue_ring) <= PAGE_SIZE, "split ring alignment and extent");

phys_addr_t virtqueue_descriptor_address(const struct virtqueue *queue)
{
  return queue->physical + offsetof(struct virtqueue_ring, descriptors);
}

phys_addr_t virtqueue_available_address(const struct virtqueue *queue)
{
  return queue->physical + offsetof(struct virtqueue_ring, available);
}

phys_addr_t virtqueue_used_address(const struct virtqueue *queue)
{
  return queue->physical + offsetof(struct virtqueue_ring, used);
}

enum mm_result virtqueue_allocate(struct virtqueue *queue, unsigned index,
    unsigned maximum, uintptr_t notify_address)
{
  KASSERT(!queue->storage && !queue->physical);
  if (maximum < 2 || (maximum & (maximum - 1)) || index > UINT16_MAX) {
    return MM_INVALID;
  }

  uintptr_t storage;
  enum mm_result result = vm_reserve(vm_kernel_space(), VIRTQUEUE_STORAGE_BYTES,
                                     PAGE_SIZE, &storage);
  if (result != MM_OK) {
    return result;
  }
  phys_addr_t physical = pmm_alloc(VIRTQUEUE_STORAGE_BYTES / PAGE_SIZE);
  if (!physical) {
    KASSERT(vm_release(vm_kernel_space(), storage, VIRTQUEUE_STORAGE_BYTES) == MM_OK);
    return MM_NO_MEMORY;
  }

  size_t mapped = 0;
  while (mapped < VIRTQUEUE_STORAGE_BYTES) {
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
    pmm_free(physical, VIRTQUEUE_STORAGE_BYTES / PAGE_SIZE);
    KASSERT(vm_release(vm_kernel_space(), storage, VIRTQUEUE_STORAGE_BYTES) == MM_OK);
    return result;
  }

  memset((void *)storage, 0, VIRTQUEUE_STORAGE_BYTES);
  *queue = (struct virtqueue){
    .storage = storage,
    .physical = physical,
    .ring = (volatile struct virtqueue_ring *)storage,
    .request = (void *)(storage + VIRTQUEUE_REQUEST_OFFSET),
    .reply = (void *)(storage + VIRTQUEUE_REPLY_OFFSET),
    .notify_address = notify_address,
    .index = index,
    .size = maximum < VIRTQUEUE_DESCRIPTORS ? maximum : VIRTQUEUE_DESCRIPTORS,
  };
  return MM_OK;
}

void virtqueue_release(struct virtqueue *queue)
{
  if (!queue->storage) {
    return;
  }
  for (size_t offset = 0; offset < VIRTQUEUE_STORAGE_BYTES; offset += PAGE_SIZE) {
    phys_addr_t frame;
    KASSERT(vm_unmap(vm_kernel_space(), queue->storage + offset, &frame) == MM_OK);
    KASSERT(frame == queue->physical + offset);
  }
  pmm_free(queue->physical, VIRTQUEUE_STORAGE_BYTES / PAGE_SIZE);
  KASSERT(vm_release(vm_kernel_space(), queue->storage, VIRTQUEUE_STORAGE_BYTES) == MM_OK);
  *queue = (struct virtqueue){0};
}

bool virtqueue_submit(struct virtqueue *queue, size_t request_bytes, size_t reply_bytes)
{
  if (queue->in_flight || !request_bytes || request_bytes > VIRTQUEUE_REQUEST_BYTES ||
      reply_bytes > VIRTQUEUE_REPLY_BYTES) {
    return false;
  }

  volatile struct virtqueue_ring *ring = queue->ring;
  ring->descriptors[VIRTQUEUE_REQUEST_DESCRIPTOR] = (struct virtqueue_descriptor){
    .address = queue->physical + VIRTQUEUE_REQUEST_OFFSET,
    .length = request_bytes,
    .flags = reply_bytes ? VIRTQUEUE_DESC_NEXT : 0,
    .next = VIRTQUEUE_REPLY_DESCRIPTOR,
  };
  if (reply_bytes) {
    ring->descriptors[VIRTQUEUE_REPLY_DESCRIPTOR] = (struct virtqueue_descriptor){
      .address = queue->physical + VIRTQUEUE_REPLY_OFFSET,
      .length = reply_bytes,
      .flags = VIRTQUEUE_DESC_WRITE,
    };
  }
  queue->reply_capacity = reply_bytes;
  queue->in_flight = true;
  ring->available.ring[queue->available % queue->size] = VIRTQUEUE_REQUEST_DESCRIPTOR;
  /* Publish buffers/descriptors before the index that lends them to the device. */
  dma_write_barrier();
  ring->available.index = ++queue->available;

  /* The device may suppress kicks while polling. Publish before checking its
   * flag, so neither side misses the other's transition to/from idle. */
  dma_full_barrier();
  if (!(ring->used.flags & VIRTQUEUE_USED_NO_NOTIFY)) {
    *(volatile uint16_t *)queue->notify_address = queue->index;
  }
  return true;
}

enum virtqueue_result virtqueue_complete(struct virtqueue *queue, size_t *reply_bytes)
{
  volatile struct virtqueue_ring *ring = queue->ring;
  uint16_t used = ring->used.index;
  if (used == queue->used) {
    return VIRTQUEUE_PENDING;
  }
  if (!queue->in_flight || (uint16_t)(used - queue->used) != 1) {
    return VIRTQUEUE_BROKEN;
  }

  /* Used index transfers ownership back; only then read the entry and payload. */
  dma_read_barrier();
  unsigned slot = queue->used % queue->size;
  uint32_t id = ring->used.ring[slot].id;
  uint32_t length = ring->used.ring[slot].length;
  if (id != VIRTQUEUE_REQUEST_DESCRIPTOR || length > queue->reply_capacity) {
    return VIRTQUEUE_BROKEN;
  }
  queue->used = used;
  queue->in_flight = false;
  *reply_bytes = length;
  return VIRTQUEUE_COMPLETE;
}
