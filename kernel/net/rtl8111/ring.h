#ifndef RTL8111_RING_H
#define RTL8111_RING_H

#include <kernel/mm/dma.h>

#define RTL_RING_COUNT 32u
#define RTL_BUFFER_BYTES 2048u

#define RTL_DESCRIPTOR_OWN (UINT32_C(1) << 31)
#define RTL_DESCRIPTOR_EOR (UINT32_C(1) << 30)
#define RTL_DESCRIPTOR_FS (UINT32_C(1) << 29)
#define RTL_DESCRIPTOR_LS (UINT32_C(1) << 28)
#define RTL_DESCRIPTOR_LENGTH_MASK UINT32_C(0x3fff)

struct rtl_descriptor {
  uint32_t opts1;
  uint32_t opts2;
  uint64_t address;
};

/* One fixed buffer per descriptor. DMA addresses remain unchanged after
 * allocation. Only the BSP network worker services runtime rings and buffers;
 * interrupt entry only acknowledges the device and wakes that worker. */
struct rtl_ring {
  struct dma_buffer storage;
  volatile struct rtl_descriptor *descriptors;
  unsigned producer, consumer, outstanding;
  bool receive;
};

/* BSP/IF=0 before AP startup. RX and TX each own a separate allocation.
 * Release only unpublished boot storage after the caller confirms device
 * quiescence. Published storage and mappings remain until reboot. */
enum mm_result rtl_ring_allocate(struct rtl_ring *ring, bool receive);
void rtl_ring_release(struct rtl_ring *ring);

/* The caller must have checked CPU ownership and completed the DMA read barrier
 * before borrowing a completed buffer. The borrow ends when OWN is published. */
void *rtl_ring_buffer(const struct rtl_ring *ring, unsigned id);
void rtl_ring_repost_receive(struct rtl_ring *ring, unsigned id);

#endif
