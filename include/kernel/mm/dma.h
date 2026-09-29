#ifndef KERNEL_MM_DMA_H
#define KERNEL_MM_DMA_H

#include <kernel/mm/types.h>

/* Coherent, physically contiguous kernel storage. CPU and device addresses are
 * distinct. bytes is the page-rounded allocation extent. */
struct dma_buffer {
  uintptr_t address;
  phys_addr_t physical;
  size_t bytes;
};

/* BSP/IF=0 before AP startup. Zero-initialized on success; failure leaves an
 * empty buffer. Release only unpublished storage or after confirmed device
 * reset. Runtime mappings/storage remain until reboot. */
enum mm_result dma_buffer_allocate(struct dma_buffer *buffer, size_t bytes);
void dma_buffer_release(struct dma_buffer *buffer);

#endif
