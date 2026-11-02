#ifndef KERNEL_MM_DEVICE_PAGES_H
#define KERNEL_MM_DEVICE_PAGES_H

#include <kernel/mm/types.h>

enum device_pages_cache { DEVICE_PAGES_WB, DEVICE_PAGES_WC };

/* Stable owner-held storage. FRAMES is a borrowed, immutable physical-page
 * vector, not a DMA-address vector. Allocation admits no device access: the
 * device owner must separately establish its address translation/coherency
 * contract before publishing any GPU/DMA reference. */
struct device_pages {
  uintptr_t address;
  const phys_addr_t *frames;
  size_t bytes;
  enum device_pages_cache cache;
  bool quarantined;
};

/* Fixed BSP kernel task, IF=1, kernel root, no short lock or mapping loan.
 * PAGES must be empty. Zero-filled, page-rounded, RW/NX WB or WC on success;
 * failure leaves it empty. The operation owns shared VM mutation internally
 * and yields between PMM_FRAME_BATCH_MAX-page steps. No arbitrary size cap,
 * contiguous-physical assumption, IOMMU mutation or device publication. */
enum mm_result device_pages_allocate(struct device_pages *pages, size_t bytes,
    enum device_pages_cache cache);

/* Same context. Caller has closed/refill-stopped and joined every CPU/device
 * consumer, including device cache/translation retirement where required.
 * CONFIRM_REMOTE is required once the CPU mapping was published beyond the
 * allocating actor; false is only the unpublished/local lifetime proof.
 * MM_BUSY with quarantined=false is unchanged owner-admission refusal.
 * An unconfirmed remote flush keeps mappings/vector/backing, quarantines the
 * reservation and sets quarantined=true; never retry/reuse it before reboot.
 * MM_OK actually unmaps/frees and empties PAGES. */
enum mm_result device_pages_release(struct device_pages *pages, bool confirm_remote);

#endif
