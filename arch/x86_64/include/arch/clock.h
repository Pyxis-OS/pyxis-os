#ifndef ARCH_CLOCK_H
#define ARCH_CLOCK_H

#include <kernel/boot.h>

/* BSP only: discover before replacing Limine's root, initialize after mapping
 * the register block and before publishing any tasks or starting APs. */
void arch_clock_prepare(const struct boot_info *boot);
uint64_t arch_clock_physical_address(void);
void arch_clock_init(void);

/* Any CPU after initialization. Shared monotonic nanoseconds from clock
 * initialization; no calendar epoch or promise to count VM pause/suspend time. */
uint64_t arch_monotonic_ns(void);

#endif
