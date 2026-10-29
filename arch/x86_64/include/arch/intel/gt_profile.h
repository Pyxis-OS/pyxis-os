#ifndef ARCH_INTEL_GT_PROFILE_H
#define ARCH_INTEL_GT_PROFILE_H

#include <stdbool.h>
#include <stdint.h>
#include <kernel/boot.h>

/* One boot-only transaction, BSP/IF=0. Caller owns the matched PTL endpoint,
 * stable BAR0/D0/display DC0 and exclusive GGTT/GT submission ownership.
 * Pyxis must not start GuC/GSC or submit a context during admission or later
 * without revisiting PAT ownership. Preparation holds the qualified domains;
 * admission rechecks the accepted boot submitter proof and the caller's full
 * inherited GGTT PAT3-use scan. Only PAT3 may be programmed, to 0x3c. No reset
 * or firmware commands. Failure prohibits further writes; uncertain cleanup
 * retains mappings and requests until reboot. */
bool intel_gt_profile_prepare(const struct boot_info *boot, uint64_t bar);
/* Call after complete GGTT import under the same exclusive bootstrap owner. */
bool intel_gt_profile_admit_pat(bool ggtt_pat3_unused);
/* On true, the caller must retain its display power holds and MMIO mappings. */
bool intel_gt_profile_fenced(void);
/* Check before each new PTE write; does not acquire the MCR semaphore again. */
bool intel_gt_profile_ready(void);
/* Successful admission selected a UC/noncoherent entry in 0..3, inherited or
 * the conditionally programmed PAT3. Only new UMA PTEs may use it. */
unsigned intel_gt_profile_pat_index(void);
/* Call after EVERY aligned 64-bit UC CPU PTE write, including error rollback.
 * This is the stepping-required write-completion workaround, not a TLB flush. */
bool intel_gt_note_ggtt_write(void);
/* Drain partial workaround batch, post writes, release only our forcewake bits.
 * On success the transaction is closed; no further PTE writes are admitted. */
bool intel_gt_profile_finish(void);

#endif
