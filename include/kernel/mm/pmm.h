#ifndef KERNEL_MM_PMM_H
#define KERNEL_MM_PMM_H
#include <kernel/boot.h>
#include <kernel/mm/types.h>

/* Exclusive bitmap coverage limit; usable RAM beyond it is ignored. */
#define PMM_MAX_PHYS (UINT64_C(64) << 30)
/* Bounded transaction-owned vectors, not a retained per-CPU free pool. */
#define PMM_FRAME_BATCH_MAX 64

struct pmm_bootstrap {
  phys_addr_t metadata_phys;
  size_t metadata_pages;
  size_t frame_count;
};

struct pmm_stats {
  size_t total_frames; /* Allocatable capacity, excluding permanent reservations. */
  size_t free_frames;
  size_t allocated_frames;
  size_t metadata_pages;
};

/* Plan, init and rebase run once on the BSP before any other CPU starts.
 * Plan reserves space conceptually; arch supplies a bootstrap-accessible pointer. */
struct pmm_bootstrap pmm_plan(const struct boot_info *boot);
void pmm_init(const struct boot_info *boot, struct pmm_bootstrap plan,
              void *metadata);
void pmm_rebase(void *metadata);

/* Afterwards any CPU may call these with IF=0, outside interrupt/fault entry.
 * A short leaf lock serializes the bitmap and statistics. Allocation can then
 * notify memory pressure, so its caller must not hold the queue or pressure
 * lock. Frames are returned unzeroed. */
phys_addr_t pmm_alloc(size_t pages); /* Zero means exhaustion or invalid count. */
/* As pmm_alloc, but every returned byte is at or below MAXIMUM, for devices
 * with a limited DMA address width. */
phys_addr_t pmm_alloc_below(size_t pages, phys_addr_t maximum);
/* Claim up to COUNT arbitrary single frames in first-fit order. COUNT is
 * 1..PMM_FRAME_BATCH_MAX and FRAMES is caller-owned kernel storage. The returned
 * prefix is allocated/accounted immediately, unzeroed, and owned by the caller
 * even if shorter than COUNT. No lock survives return; callers zero/map or
 * unwind the prefix. No unused frames are retained between calls. */
size_t pmm_alloc_frames(phys_addr_t *frames, size_t count);
/* Caller owns precisely this extent; allocation boundaries are not tracked.
 * Misaligned, reserved, out-of-range and already-free frames are fatal errors. */
void pmm_free(phys_addr_t physical, size_t pages);
/* Release COUNT distinct caller-owned single frames, 1..PMM_FRAME_BATCH_MAX.
 * Validate the entire vector before clearing any allocation bit. Reserved,
 * duplicate, misaligned, out-of-range and already-free frames are fatal errors,
 * as for pmm_free. The vector may be freed by a CPU other than its allocator. */
void pmm_free_frames(const phys_addr_t *frames, size_t count);
struct pmm_stats pmm_get_stats(void);

#endif
