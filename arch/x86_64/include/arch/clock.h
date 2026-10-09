#ifndef ARCH_CLOCK_H
#define ARCH_CLOCK_H

#include <kernel/boot.h>

/* BSP only: discover before replacing Limine's root, initialize after mapping
 * the register block and before publishing any tasks or starting APs. */
void arch_clock_prepare(const struct boot_info *boot);
uint64_t arch_clock_physical_address(void);
void arch_clock_init(void);

/* Any CPU after initialization: incorporate a counter sample when software
 * extension is selected. No MMIO read on the direct 64-bit path. Boot callers
 * must sample between phases and within long polling/iteration sequences. */
void arch_clock_maintain(void);

/* BSP LAPIC timer handler, IF=0: maintain at the configured delivery interval.
 * Delays must still leave less than one hardware wrap between samples. */
void arch_clock_tick(void);

/* Any CPU after initialization. Shared monotonic nanoseconds from clock
 * initialization. Software extension requires less than one advancing-counter
 * wrap between samples, including boot, stalls and debugger/VM pauses. Reboot
 * after violating that bound; missed wraps cannot be detected or recovered. */
uint64_t arch_monotonic_ns(void);

/* TSC selection. arch_clock_init calibrates the TSC against the HPET on the
 * BSP; boot keeps reading the HPET until arch_clock_select. Startup is
 * serialized, one AP at a time, all with IF=0:
 * - the AP calls arch_clock_ap_prepare before publishing itself online, then
 *   arch_clock_ap_check;
 * - the BSP calls arch_clock_bsp_check once it sees that AP online.
 * The two checks run together, comparing the CPUs' TSCs for about 2 ms. */
void arch_clock_ap_prepare(void);
void arch_clock_ap_check(void);
void arch_clock_bsp_check(size_t ap_index);
/* BSP, after the last AP's check and before the scheduler starts. Switch every
 * CPU to the TSC if all checks passed, keeping the epoch, or keep the HPET.
 * Logs the choice. */
void arch_clock_select(void);

#endif
