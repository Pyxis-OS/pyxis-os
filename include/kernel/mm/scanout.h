#ifndef KERNEL_MM_SCANOUT_H
#define KERNEL_MM_SCANOUT_H
#include <kernel/boot.h>
#include <kernel/mm/types.h>
struct scanout_storage { uintptr_t address; phys_addr_t physical; uint64_t offset; size_t bytes; };
/* Same exclusion checks as prepare, without reservation, mapping or stores.
 * On success address remains zero; the result is evidence, not an allocation. */
bool scanout_candidate(const struct boot_info *boot, phys_addr_t pool, size_t pool_bytes,
    size_t occupied_end, size_t payload, struct scanout_storage *storage);
/* BSP IF=0 before AP startup. Caller proves hardware pool and all firmware/live
 * exclusions; this policy preserves low GOP/VGA storage and a 16MiB tail guard.
 * Only reserved UMA, one WC mapping, pinned until reboot; no PMM allocation. */
bool scanout_prepare(const struct boot_info *boot, phys_addr_t pool, size_t pool_bytes,
    size_t gop_end, size_t payload, struct scanout_storage *storage);
/* Private, never published to GPU or panic. Bootstrap failure unwind only. */
void scanout_discard(struct scanout_storage *storage);

struct scanout_pages {
  uintptr_t address;
  phys_addr_t *frames;
  size_t bytes;
};
/* BSP/IF=0 before AP startup. Allocate caller-owned PMM pages with one RW/NX/WC
 * mapping and no WB aliases; backing is pinned once published to the GPU.
 * Only discard an unpublished result, before any device reference exists. */
bool scanout_pages_prepare(size_t bytes, uint64_t deadline, struct scanout_pages *storage);
void scanout_pages_discard(struct scanout_pages *storage);
#endif
