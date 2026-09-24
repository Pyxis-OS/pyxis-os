#include <arch/acpi.h>
#include <arch/clock.h>
#include <arch/layout.h>
#include <kernel/log.h>
#include <kernel/panic.h>

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

static uint64_t hpet_physical;
static uint32_t period_fs;

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
  if (!(capabilities & HPET_COUNTER_64BIT)) {
    panic("monotonic clock requires a 64-bit HPET counter");
  }
  period_fs = hpet_read(HPET_PERIOD);
  if (!period_fs || period_fs > HPET_MAX_PERIOD_FS) {
    panic("invalid HPET counter period");
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
  hpet_write(HPET_CONFIGURATION, HPET_ENABLE);
  klog("clock: HPET 64-bit monotonic counter, period=%u fs\n", period_fs);
}

uint64_t arch_monotonic_ns(void)
{
  uint32_t high, low;
  /* Some implementations split 64-bit MMIO reads. Retry if the low word
   * rolled over, rather than publishing a torn counter value on any CPU. */
  do {
    high = hpet_read(HPET_COUNTER + sizeof(uint32_t));
    low = hpet_read(HPET_COUNTER);
  } while (high != hpet_read(HPET_COUNTER + sizeof(uint32_t)));

  uint64_t ticks = ((uint64_t)high << 32) | low;
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
