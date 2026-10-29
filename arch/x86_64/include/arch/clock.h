#ifndef ARCH_CLOCK_H
#define ARCH_CLOCK_H

#include <kernel/boot.h>

/* BSP only: discover before replacing Limine's root, initialize after mapping
 * the register block and before publishing any tasks or starting APs. */
void arch_clock_prepare(const struct boot_info *boot);
uint64_t arch_clock_physical_address(void);
void arch_clock_init(void);
/* Intel's CPUID 0x15 core crystal in Hz, or zero when it is not enumerated
 * (always on AMD). Valid after arch_clock_init. */
uint64_t arch_clock_crystal_hz(void);
/* HPET time in nanoseconds, for calibration before the TSC is selected. */
uint64_t arch_clock_hpet_ns(void);

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

/* Unordered TSC read for CPU-time accounting. Only differences between two
 * reads on the same CPU are meaningful; it is never a clock. */
static inline uint64_t arch_tsc_read(void)
{
  uint32_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((uint64_t)high << 32) | low;
}
/* IF=0, after clock selection: CYCLES of arch_tsc_read() differences as
 * nanoseconds, from the calibrated TSC rate or, under KVM, this vCPU's
 * published scale. Zero when neither rate is known. */
uint64_t arch_tsc_cycles_ns(uint64_t cycles);
/* Static name of the current timestamp source, for measurement provenance. */
const char *arch_clock_source(void);

/* Source admission. BSP/AP initialization registers resident KVM time records
 * when advertised, without requiring invariant TSC. Native qualification remains:
 * arch_clock_init calibrates the TSC against the HPET on the BSP; boot keeps
 * reading the HPET until arch_clock_select. On HPET timeout,
 * a qualified CPUID frequency supplies provisional BSP time and AP time after
 * that AP passes its read-kind and warp checks;
 * failed later qualification is fatal, and selection preserves that epoch. Startup is
 * serialized, one AP at a time, all with IF=0:
 * - the AP calls arch_clock_ap_prepare before publishing itself online, then
 *   arch_clock_ap_check;
 * - the BSP calls arch_clock_bsp_check once it sees that AP online.
 * The two checks run together, comparing the CPUs' TSCs for about 2 ms. */
void arch_clock_ap_prepare(void);
void arch_clock_ap_check(void);
void arch_clock_bsp_check(size_t ap_index);
/* BSP, after the last AP's check and before the scheduler starts. Select KVM
 * if every vCPU has a usable registered record; otherwise qualified TSC or
 * healthy HPET. One common epoch is preserved. KVM reads share a cross-CPU
 * maximum, including fallback reads during admission. Logs the choice. */
void arch_clock_select(void);

#endif
