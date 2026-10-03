#include <arch/acpi.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/layout.h>
#include <kernel-config.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <stdatomic.h>

#define HPET_CAPABILITIES 0x000
#define HPET_PERIOD 0x004
#define HPET_CONFIGURATION 0x010
#define HPET_COUNTER 0x0f0
#define HPET_COUNTER_64BIT (1u << 13)
#define HPET_TIMER_COUNT_SHIFT 8
#define HPET_TIMER_COUNT_MASK 0x1f
#define HPET_TIMER_CONFIGURATION 0x100
#define HPET_TIMER_STRIDE 0x20
#define HPET_TIMER_INTERRUPT_ENABLE (1u << 2)
#define HPET_TIMER_FSB_ENABLE (1u << 14)
#define HPET_ENABLE (1u << 0)
#define HPET_MAX_PERIOD_FS UINT32_C(100000000)
#define FEMTOSECONDS_PER_NANOSECOND UINT64_C(1000000)
#define FEMTOSECONDS_PER_SECOND UINT64_C(1000000000000000)
#define HPET_COUNTER_WRAP_TICKS (UINT64_C(1) << 32)

static uint64_t hpet_physical;
static uint32_t period_fs;
/* Source/period are immutable before AP startup. Only BSP timer dispatch
 * mutates the countdown; all readers publish to the shared tick accumulator. */
static bool software_extended;
static uint32_t maintenance_ticks;
static _Atomic uint64_t extended_ticks;

static uint32_t hpet_read(unsigned offset)
{
  return *(volatile uint32_t *)(HPET_BASE + offset);
}

static void hpet_write(unsigned offset, uint32_t value)
{
  *(volatile uint32_t *)(HPET_BASE + offset) = value;
}

void arch_clock_prepare(const struct boot_info *boot)
{
  hpet_physical = acpi_hpet_address(boot);
  if (hpet_physical & (ARCH_PAGE_SIZE - 1)) {
    panic("HPET register block is not page aligned");
  }
}

uint64_t arch_clock_physical_address(void)
{
  return hpet_physical;
}

void arch_clock_init(void)
{
  uint32_t capabilities = hpet_read(HPET_CAPABILITIES);
  unsigned counter_bits = capabilities & HPET_COUNTER_64BIT ? 64 : 32;
  software_extended = counter_bits == 32;
  period_fs = hpet_read(HPET_PERIOD);
  if (!period_fs || period_fs > HPET_MAX_PERIOD_FS) {
    panic("invalid HPET counter period");
  }
  maintenance_ticks = CONFIG_HPET_MAINTENANCE_TICKS;
  if (software_extended) {
    /* Compare durations without truncation or 128-bit division helpers. The
     * configured nominal interval must leave room before one hardware wrap;
     * supported execution also bounds actual interrupt/reader delays. */
    __uint128_t interval = (__uint128_t)maintenance_ticks * FEMTOSECONDS_PER_SECOND;
    __uint128_t wrap = (__uint128_t)HPET_COUNTER_WRAP_TICKS * period_fs *
                      arch_timer_frequency();
    if (interval >= wrap) {
      panic("HPET maintenance interval reaches a full counter wrap");
    }
  }

  /* Stop before setting the epoch. Keep legacy replacement disabled: PIT/RTC
   * routing and APIC scheduling remain independent of this clock source. No
   * comparator is armed and no HPET interrupts are used. */
  hpet_write(HPET_CONFIGURATION, 0);
  unsigned timers = ((capabilities >> HPET_TIMER_COUNT_SHIFT) & HPET_TIMER_COUNT_MASK) + 1;
  for (unsigned i = 0; i < timers; ++i) {
    unsigned offset = HPET_TIMER_CONFIGURATION + i * HPET_TIMER_STRIDE;
    hpet_write(offset, hpet_read(offset) &
        ~(HPET_TIMER_INTERRUPT_ENABLE | HPET_TIMER_FSB_ENABLE));
  }
  hpet_write(HPET_COUNTER, 0);
  hpet_write(HPET_COUNTER + sizeof(uint32_t), 0);
  atomic_init(&extended_ticks, 0);
  hpet_write(HPET_CONFIGURATION, HPET_ENABLE);
  klog("clock: HPET %u-bit counter, %s, period=%u fs\n", counter_bits,
       software_extended ? "software-extended" : "direct", period_fs);
  if (software_extended) {
    klog("clock: HPET maintenance every %u BSP timer ticks (~%u Hz)\n",
         maintenance_ticks, arch_timer_frequency());
  }
}

static uint64_t hpet_extended_ticks(void)
{
  for (;;) {
    uint64_t last = atomic_load_explicit(&extended_ticks, memory_order_acquire);
    if (last == UINT64_MAX) {
      return UINT64_MAX;
    }
    /* Acquire keeps this fresh UC read after the shared snapshot. UC loads
     * retain hardware ordering; the locked CAS orders publication. An
     * interrupting reader never waits for its interrupted reader to finish. */
    uint32_t low = hpet_read(HPET_COUNTER);
    uint32_t delta = low - (uint32_t)last;
    uint64_t ticks = delta > UINT64_MAX - last ? UINT64_MAX : last + delta;
    if (atomic_compare_exchange_weak_explicit(&extended_ticks, &last, ticks,
          memory_order_acq_rel, memory_order_relaxed)) {
      return ticks;
    }
    /* Never reuse low against the newer snapshot returned by a failed CAS:
     * that stale sample could fabricate almost a whole wrap. */
  }
}

void arch_clock_maintain(void)
{
  if (software_extended) {
    hpet_extended_ticks();
  }
}

void arch_clock_tick(void)
{
  if (software_extended && !--maintenance_ticks) {
    arch_clock_maintain();
    maintenance_ticks = CONFIG_HPET_MAINTENANCE_TICKS;
  }
}

static uint64_t hpet_direct_ticks(void)
{
  uint32_t high, low;
  /* Some implementations split 64-bit MMIO reads. Retry if the low word
   * rolled over, rather than publishing a torn counter value on any CPU. */
  do {
    high = hpet_read(HPET_COUNTER + sizeof(uint32_t));
    low = hpet_read(HPET_COUNTER);
  } while (high != hpet_read(HPET_COUNTER + sizeof(uint32_t)));

  return ((uint64_t)high << 32) | low;
}

uint64_t arch_monotonic_ns(void)
{
  uint64_t ticks = software_extended ? hpet_extended_ticks() : hpet_direct_ticks();
  /* Tick exhaustion is terminal too: a saturated accumulator's low word no
   * longer corresponds to hardware and must never feed another modular delta. */
  if (software_extended && ticks == UINT64_MAX) {
    return UINT64_MAX;
  }
  uint64_t whole = ticks / FEMTOSECONDS_PER_NANOSECOND;
  uint64_t fraction = (ticks % FEMTOSECONDS_PER_NANOSECOND) * period_fs /
                      FEMTOSECONDS_PER_NANOSECOND;
  /* Split the product to avoid overflowing femtoseconds long before the
   * nanosecond representation fills. Saturate rather than wrap its epoch. */
  if (whole > (UINT64_MAX - fraction) / period_fs) {
    return UINT64_MAX;
  }
  return whole * period_fs + fraction;
}
