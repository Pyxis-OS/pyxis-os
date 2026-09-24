#ifndef ARCH_DMA_H
#define ARCH_DMA_H

/* Coherent DMA through ordinary write-back RAM on x86. The CPU orders stores
 * with stores and loads with loads; keep the compiler from crossing ownership
 * handoffs. A store followed by a device-state load needs a hardware fence. */
static inline void dma_write_barrier(void)
{
  __asm__ volatile("" : : : "memory");
}

static inline void dma_read_barrier(void)
{
  __asm__ volatile("" : : : "memory");
}

static inline void dma_full_barrier(void)
{
  __asm__ volatile("mfence" : : : "memory");
}

#endif
