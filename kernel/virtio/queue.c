#include <arch/dma.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/virtio/queue.h>

#define VIRTQUEUE_DESC_NEXT 1u
#define VIRTQUEUE_DESC_WRITE 2u
#define VIRTQUEUE_USED_NO_NOTIFY 1u
#define VIRTQUEUE_NO_DESCRIPTOR UINT16_MAX
#define VIRTQUEUE_USED_ALIGNMENT 4u

struct virtqueue_descriptor {
  uint64_t address;
  uint32_t length;
  uint16_t flags, next;
};

struct virtqueue_available {
  uint16_t flags, index, ring[];
};

struct virtqueue_used_element {
  uint32_t id, length;
};

struct virtqueue_used {
  uint16_t flags, index;
  struct virtqueue_used_element ring[];
};

/* Device-visible descriptors are never used to recover our ownership chain. */
struct virtqueue_entry {
  uint64_t request_id;
  uint32_t writable;
  uint16_t next, descriptors;
  bool active, seen;
};

_Static_assert(sizeof(struct virtqueue_descriptor) == 16, "split descriptor layout");
_Static_assert(sizeof(struct virtqueue_used_element) == 8, "used element layout");
_Static_assert(offsetof(struct virtqueue_available, ring) == 4 &&
               offsetof(struct virtqueue_used, ring) == 4, "split ring headers");

phys_addr_t virtqueue_descriptor_address(const struct virtqueue *queue)
{
  return queue->storage.physical;
}

phys_addr_t virtqueue_available_address(const struct virtqueue *queue)
{
  return queue->storage.physical + queue->available_offset;
}

phys_addr_t virtqueue_used_address(const struct virtqueue *queue)
{
  return queue->storage.physical + queue->used_offset;
}

enum mm_result virtqueue_allocate(struct virtqueue *queue, unsigned index,
    unsigned size, unsigned maximum, uintptr_t notify_address)
{
  KASSERT(!queue->storage.address && !queue->entries && !queue->snapshot);
  if (!size || size > VIRTQUEUE_MAX_SIZE || (size & (size - 1)) ||
      !maximum || maximum > VIRTQUEUE_MAX_SIZE || (maximum & (maximum - 1)) ||
      size > maximum || index > UINT16_MAX || !notify_address) {
    return MM_INVALID;
  }

  size_t available_offset = size * sizeof(struct virtqueue_descriptor);
  /* Reserve unused event fields; EVENT_IDX is not negotiated. */
  size_t used_offset = available_offset + sizeof(struct virtqueue_available) +
      (size + 1) * sizeof(uint16_t);
  used_offset = (used_offset + VIRTQUEUE_USED_ALIGNMENT - 1) &
      ~(size_t)(VIRTQUEUE_USED_ALIGNMENT - 1);
  size_t bytes = used_offset + sizeof(struct virtqueue_used) +
      size * sizeof(struct virtqueue_used_element) + sizeof(uint16_t);

  struct virtqueue_entry *entries = kmalloc(size * sizeof(*entries));
  struct virtqueue_used_element *snapshot = kmalloc(size * sizeof(*snapshot));
  if (!entries || !snapshot) {
    kfree(snapshot);
    kfree(entries);
    return MM_NO_MEMORY;
  }
  struct dma_buffer storage = {0};
  enum mm_result result = dma_buffer_allocate(&storage, bytes);
  if (result != MM_OK) {
    kfree(snapshot);
    kfree(entries);
    return result;
  }
  memset(entries, 0, size * sizeof(*entries));
  for (unsigned i = 0; i < size; ++i) {
    entries[i].next = i + 1 < size ? i + 1 : VIRTQUEUE_NO_DESCRIPTOR;
  }
  *queue = (struct virtqueue){
    .storage = storage,
    .descriptors = (volatile struct virtqueue_descriptor *)storage.address,
    .available_ring = (volatile struct virtqueue_available *)(storage.address + available_offset),
    .used_ring = (volatile struct virtqueue_used *)(storage.address + used_offset),
    .entries = entries,
    .snapshot = snapshot,
    .notify_address = notify_address,
    .available_offset = available_offset,
    .used_offset = used_offset,
    .index = index,
    .size = size,
    .free_count = size,
  };
  return MM_OK;
}

void virtqueue_release(struct virtqueue *queue)
{
  dma_buffer_release(&queue->storage);
  kfree(queue->snapshot);
  kfree(queue->entries);
  *queue = (struct virtqueue){0};
}

enum virtqueue_submit_result virtqueue_submit(struct virtqueue *queue,
    const struct virtqueue_segment *segments, size_t count, uint64_t request_id)
{
  if (queue->stopped) {
    return VIRTQUEUE_SUBMIT_STOPPED;
  }
  if (!queue->entries || !segments || !count || count > queue->size) {
    return VIRTQUEUE_SUBMIT_INVALID;
  }
  uint32_t total = 0, writable = 0;
  bool writing = false;
  for (size_t i = 0; i < count; ++i) {
    const struct virtqueue_segment *segment = &segments[i];
    if (!segment->bytes || segment->bytes > UINT32_MAX - total ||
        segment->physical > UINT64_MAX - segment->bytes ||
        (segment->access != VIRTQUEUE_DEVICE_READ && segment->access != VIRTQUEUE_DEVICE_WRITE) ||
        (writing && segment->access == VIRTQUEUE_DEVICE_READ)) {
      return VIRTQUEUE_SUBMIT_INVALID;
    }
    total += segment->bytes;
    if (segment->access == VIRTQUEUE_DEVICE_WRITE) {
      writing = true;
      writable += segment->bytes;
    }
  }
  for (unsigned i = 0; i < queue->size; ++i) {
    if (queue->entries[i].active && queue->entries[i].request_id == request_id) {
      return VIRTQUEUE_SUBMIT_INVALID;
    }
  }
  if (count > queue->free_count) {
    return VIRTQUEUE_FULL;
  }

  uint16_t head = queue->free_head;
  uint16_t descriptor = head;
  for (size_t i = 0; i < count; ++i) {
    struct virtqueue_entry *entry = &queue->entries[descriptor];
    uint16_t next = entry->next;
    bool last = i + 1 == count;
    queue->descriptors[descriptor] = (struct virtqueue_descriptor){
      .address = segments[i].physical,
      .length = segments[i].bytes,
      .flags = (segments[i].access == VIRTQUEUE_DEVICE_WRITE ? VIRTQUEUE_DESC_WRITE : 0) |
          (last ? 0 : VIRTQUEUE_DESC_NEXT),
      .next = last ? 0 : next,
    };
    if (last) {
      queue->free_head = next;
      entry->next = VIRTQUEUE_NO_DESCRIPTOR;
    }
    descriptor = next;
  }
  struct virtqueue_entry *entry = &queue->entries[head];
  entry->request_id = request_id;
  entry->writable = writable;
  entry->descriptors = count;
  entry->active = true;
  queue->free_count -= count;
  ++queue->outstanding;
  queue->available_ring->ring[queue->available % queue->size] = head;
  /* Publication, not notification, lends descriptors and payloads to the device. */
  dma_write_barrier();
  queue->available_ring->index = ++queue->available;
  return VIRTQUEUE_ACCEPTED;
}

void virtqueue_notify(const struct virtqueue *queue)
{
  if (queue->stopped) {
    return;
  }
  dma_full_barrier();
  if (!(queue->used_ring->flags & VIRTQUEUE_USED_NO_NOTIFY)) {
    *(volatile uint16_t *)queue->notify_address = queue->index;
  }
}

static enum virtqueue_result broken_queue(struct virtqueue *queue)
{
  virtqueue_stop(queue);
  return VIRTQUEUE_BROKEN;
}

enum virtqueue_result virtqueue_complete(struct virtqueue *queue,
    struct virtqueue_completion *completed, size_t capacity, size_t *count)
{
  *count = 0;
  if (queue->stopped) {
    return VIRTQUEUE_STOPPED;
  }
  if (!queue->entries || (!completed && capacity)) {
    return VIRTQUEUE_INVALID;
  }
  uint16_t used = queue->used_ring->index;
  unsigned pending = (uint16_t)(used - queue->used);
  if (pending > queue->outstanding) {
    return broken_queue(queue);
  }
  if (!pending) {
    return VIRTQUEUE_PENDING;
  }
  dma_read_barrier();
  for (unsigned i = 0; i < queue->size; ++i) {
    queue->entries[i].seen = false;
  }
  for (unsigned i = 0; i < pending; ++i) {
    unsigned slot = (uint16_t)(queue->used + i) % queue->size;
    /* Consume each device field once; reclamation uses this CPU-owned snapshot. */
    struct virtqueue_used_element element = {
      .id = queue->used_ring->ring[slot].id,
      .length = queue->used_ring->ring[slot].length,
    };
    if (element.id >= queue->size) {
      return broken_queue(queue);
    }
    struct virtqueue_entry *entry = &queue->entries[element.id];
    if (!entry->active || entry->seen || element.length > entry->writable) {
      return broken_queue(queue);
    }
    entry->seen = true;
    queue->snapshot[i] = element;
  }
  if (pending > capacity) {
    return VIRTQUEUE_INVALID;
  }

  for (unsigned i = 0; i < pending; ++i) {
    uint16_t head = queue->snapshot[i].id;
    struct virtqueue_entry *entry = &queue->entries[head];
    completed[i] = (struct virtqueue_completion){
      .request_id = entry->request_id,
      .written = queue->snapshot[i].length,
    };
    entry->active = false;
    uint16_t descriptor = head;
    for (unsigned n = 0; n < entry->descriptors; ++n) {
      uint16_t next = queue->entries[descriptor].next;
      queue->entries[descriptor].next = queue->free_head;
      queue->free_head = descriptor;
      descriptor = next;
    }
    queue->free_count += entry->descriptors;
  }
  queue->outstanding -= pending;
  queue->used = used;
  *count = pending;
  return VIRTQUEUE_COMPLETE;
}

void virtqueue_stop(struct virtqueue *queue)
{
  queue->stopped = true;
}

void virtqueue_confirm_reset(struct virtqueue *queue)
{
  virtqueue_stop(queue);
  for (unsigned i = 0; i < queue->size; ++i) {
    queue->entries[i] = (struct virtqueue_entry){.next = VIRTQUEUE_NO_DESCRIPTOR};
  }
  queue->outstanding = 0;
  queue->free_count = 0;
  queue->free_head = VIRTQUEUE_NO_DESCRIPTOR;
}
