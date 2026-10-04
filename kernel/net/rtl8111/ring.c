#include <arch/dma.h>
#include <kernel/panic.h>
#include "ring.h"

#define RTL_STORAGE_BYTES (PAGE_SIZE + RTL_RING_COUNT * RTL_BUFFER_BYTES)

_Static_assert(sizeof(struct rtl_descriptor) == 16, "RTL descriptor size");
_Static_assert(RTL_RING_COUNT * sizeof(struct rtl_descriptor) <= PAGE_SIZE,
               "descriptor bank must fit before the buffer bank");
_Static_assert(offsetof(struct rtl_descriptor, opts1) == 0 &&
               offsetof(struct rtl_descriptor, opts2) == 4 &&
               offsetof(struct rtl_descriptor, address) == 8, "RTL descriptor layout");

enum mm_result rtl_ring_allocate(struct rtl_ring *ring, bool receive)
{
  KASSERT(!ring->storage.address && !ring->storage.physical && !ring->storage.bytes);
  struct dma_buffer storage = {0};
  enum mm_result result = dma_buffer_allocate(&storage, RTL_STORAGE_BYTES);
  if (result != MM_OK) {
    return result;
  }

  *ring = (struct rtl_ring){
    .storage = storage,
    .descriptors = (volatile struct rtl_descriptor *)storage.address,
    .receive = receive,
  };
  for (unsigned id = 0; id < RTL_RING_COUNT; ++id) {
    ring->descriptors[id].address = storage.physical + PAGE_SIZE + id * RTL_BUFFER_BYTES;
    ring->descriptors[id].opts1 = id == RTL_RING_COUNT - 1 ? RTL_DESCRIPTOR_EOR : 0;
    if (receive) {
      rtl_ring_repost_receive(ring, id);
    }
  }
  return MM_OK;
}

void rtl_ring_release(struct rtl_ring *ring)
{
  dma_buffer_release(&ring->storage);
  *ring = (struct rtl_ring){0};
}

void *rtl_ring_buffer(const struct rtl_ring *ring, unsigned id)
{
  KASSERT(ring->storage.address && id < RTL_RING_COUNT);
  return (void *)(ring->storage.address + PAGE_SIZE + id * RTL_BUFFER_BYTES);
}

void rtl_ring_repost_receive(struct rtl_ring *ring, unsigned id)
{
  KASSERT(ring->receive && id < RTL_RING_COUNT);
  KASSERT(!(ring->descriptors[id].opts1 & RTL_DESCRIPTOR_OWN));
  ring->descriptors[id].opts2 = 0;
  uint32_t opts1 = RTL_DESCRIPTOR_OWN | RTL_BUFFER_BYTES;
  if (id == RTL_RING_COUNT - 1) {
    opts1 |= RTL_DESCRIPTOR_EOR;
  }
  dma_write_barrier();
  ring->descriptors[id].opts1 = opts1;
}
