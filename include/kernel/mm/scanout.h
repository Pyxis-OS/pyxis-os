#ifndef KERNEL_MM_SCANOUT_H
#define KERNEL_MM_SCANOUT_H
#include <kernel/boot.h>
#include <kernel/mm/types.h>
struct scanout_storage { uintptr_t address; phys_addr_t physical; uint64_t offset; size_t bytes; };
/* BSP IF=0 before AP startup. Caller proves hardware pool and all firmware/live
 * exclusions; this policy preserves low GOP/VGA storage and a 16MiB tail guard.
 * Only reserved UMA, one WC mapping, pinned until reboot; no PMM allocation. */
bool scanout_prepare(const struct boot_info *boot, phys_addr_t pool, size_t pool_bytes,
    size_t gop_end, size_t payload, struct scanout_storage *storage);
/* Private, never published to GPU or panic. Bootstrap failure unwind only. */
void scanout_discard(struct scanout_storage *storage);
#endif
