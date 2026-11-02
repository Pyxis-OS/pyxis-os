#ifndef KERNEL_MM_HEAP_H
#define KERNEL_MM_HEAP_H
#include <stddef.h>

struct heap_stats {
  size_t pools, pool_bytes, live_allocations, live_block_bytes;
  /* Arena addresses consumed, including those retired by failed growth. */
  size_t arena_bytes, retired_bytes;
  size_t retiring_bytes, reclaimed_bytes, quarantined_bytes;
};

bool heap_init(void);
/* Boot BSP, IF=0, after task_init: start retained pool cleanup. */
void heap_reclaim_init(void);
/* At least 16-byte alignment. Zero size, oversized requests and exhaustion
 * return NULL. kfree(NULL) is harmless; other pointers must be live kmalloc
 * results. Wholly free pools detach immediately; BSP cleanup returns their
 * frames after remote TLB withdrawal. A failed withdrawal quarantines mapping,
 * backing and cleanup ownership. Arena addresses remain consumed; a used-up
 * arena is exhaustion. Any CPU with IF=0, outside interrupt/fault entry. Callers hold
 * no queue, pressure or PMM lock, because growth allocates frames. */
void *kmalloc(size_t bytes);
void kfree(void *pointer);
struct heap_stats heap_get_stats(void);

#endif
