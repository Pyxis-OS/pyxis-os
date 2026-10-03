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

#endif
