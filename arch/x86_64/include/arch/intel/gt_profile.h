#ifndef ARCH_INTEL_GT_PROFILE_H
#define ARCH_INTEL_GT_PROFILE_H

#include <stdbool.h>
#include <stdint.h>
#include <kernel/boot.h>

/* One boot-only transaction, BSP/IF=0. Caller owns the matched PTL endpoint,
 * keeps it in D0 with stable BAR0 and display DC0, and excludes other CPU GGTT
 * writers. No PAT programming, reset, firmware load or GT invalidation occurs.
 * prepare holds GT forcewake until finish. Failure prohibits further writes;
 * uncertain hardware/cleanup retains mappings and requests until reboot. */
bool intel_gt_profile_prepare(const struct boot_info *boot, uint64_t bar);
/* Check before each new PTE write; does not acquire the MCR semaphore again. */
bool intel_gt_profile_ready(void);
/* Call after EVERY aligned 64-bit UC CPU PTE write, including error rollback.
 * This is the stepping-required write-completion workaround, not a TLB flush. */
bool intel_gt_note_ggtt_write(void);
/* Drain partial workaround batch, post writes, release only our forcewake bits.
 * On success the transaction is closed; no further PTE writes are admitted. */
bool intel_gt_profile_finish(void);

#endif
