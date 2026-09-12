#ifndef KERNEL_MM_PMM_H
#define KERNEL_MM_PMM_H
#include <kernel/boot.h>
#include <kernel/mm/types.h>

/* Bounded bitmap coverage: reject usable RAM extending above 64 GiB. */
#define PMM_MAX_PHYS (UINT64_C(64) << 30)

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

/* Plan reserves space conceptually; arch supplies a bootstrap-accessible pointer. */
struct pmm_bootstrap pmm_plan(const struct boot_info *boot);
void pmm_init(const struct boot_info *boot, struct pmm_bootstrap plan,
              void *metadata);
void pmm_rebase(void *metadata);
phys_addr_t pmm_alloc(size_t pages); /* Zero means exhaustion or invalid count. */
/* Caller owns precisely this extent; allocation boundaries are not tracked.
 * Misaligned, reserved, out-of-range and already-free frames are fatal errors. */
void pmm_free(phys_addr_t physical, size_t pages);
struct pmm_stats pmm_get_stats(void);

#endif
