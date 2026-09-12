#ifndef KERNEL_MM_HEAP_H
#define KERNEL_MM_HEAP_H
#include <stddef.h>

struct heap_stats {
  size_t pools, pool_bytes, live_allocations, live_block_bytes;
};

bool heap_init(void);
/* At least 16-byte alignment. Zero size, oversized requests and exhaustion
 * return NULL. kfree(NULL) is harmless; other pointers must be live kmalloc
 * results. Pools stay VM-owned until shutdown. Single CPU, IF=0, no fault use. */
void *kmalloc(size_t bytes);
void kfree(void *pointer);
struct heap_stats heap_get_stats(void);

#endif
