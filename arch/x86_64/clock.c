#include <arch/acpi.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/layout.h>
#include <arch/smp.h>
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
#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)

#define CPUID_VENDOR 0
/* "GenuineIntel" and "AuthenticAMD", as CPUID returns them in EBX, EDX, ECX. */
#define CPUID_INTEL_EBX 0x756e6547
#define CPUID_INTEL_EDX 0x49656e69
#define CPUID_INTEL_ECX 0x6c65746e
#define CPUID_AMD_EBX 0x68747541
#define CPUID_AMD_EDX 0x69746e65
#define CPUID_AMD_ECX 0x444d4163
#define CPUID_FEATURES 1
#define CPUID_FEATURE_TSC (1u << 4)            /* EDX */
#define CPUID_FEATURE_HYPERVISOR (1u << 31)    /* ECX */
#define CPUID_TSC_RATIO 0x15
#define CPUID_EXTENDED_MAX 0x80000000
#define CPUID_EXTENDED_FEATURES_LEAF 0x80000001
#define CPUID_FEATURE_RDTSCP (1u << 27)        /* EDX */
#define CPUID_POWER_MANAGEMENT 0x80000007
#define CPUID_INVARIANT_TSC (1u << 8)          /* EDX */
#define CPUID_EXTENDED_FEATURES_2 0x80000021
#define CPUID_LFENCE_ALWAYS_SERIALIZING (1u << 2) /* EAX */
#define CPUID_FAMILY_SHIFT 8
#define CPUID_FAMILY_MASK 0xf
#define CPUID_EXTENDED_FAMILY_SHIFT 20
#define CPUID_EXTENDED_FAMILY_MASK 0xff
#define AMD_FAMILY_10H 0x10
#define AMD_DE_CFG 0xc0011029
#define AMD_DE_CFG_LFENCE_SERIALIZE (UINT64_C(1) << 1)

#define TSC_CALIBRATION_NS UINT64_C(100000000)
#define TSC_BRACKET_ATTEMPTS 8
#define TSC_MAX_ERROR_PPM 100
#define TSC_MIN_HZ UINT64_C(100000000)
#define TSC_WARP_CHECK_DIVISOR 500 /* hz / 500: about 2 ms per AP */
#define TSC_MULTIPLIER_SHIFT 32

static uint64_t hpet_physical;
static uint32_t period_fs;
/* Source/period are immutable before AP startup. Only BSP timer dispatch
 * mutates the countdown; all readers publish to the shared tick accumulator. */
static bool software_extended;
static uint32_t maintenance_ticks;
static _Atomic uint64_t extended_ticks;

/* How this CPU orders a TSC read against surrounding instructions. */
enum tsc_read_kind {
  TSC_READ_NONE,
  TSC_READ_RDTSCP_LFENCE,
  TSC_READ_CPUID,
};

/* BSP-owned until the switch. tsc_candidate turns false on the first failure,
 * with the reason kept for the single selection line. The conversion record
 * is written before tsc_selected is published and never changes after. */
static enum tsc_read_kind tsc_read_kind;
static bool tsc_candidate;
static const char *tsc_rejection;
static size_t tsc_rejected_cpu;
static uint64_t tsc_hz, tsc_error_ppm, tsc_cpuid_hz;
static uint64_t tsc_base, tsc_base_ns, tsc_multiplier;
static _Atomic bool tsc_selected;

static void tsc_calibrate(void);

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
    klog("clock: HPET maintenance every %u BSP timer ticks; BSP timer frequency %u Hz\n",
         maintenance_ticks, arch_timer_frequency());
  }
  tsc_calibrate();
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
  /* After the switch nothing reads the HPET, so its extension has no reader. */
  if (software_extended && !atomic_load_explicit(&tsc_selected, memory_order_relaxed)) {
    hpet_extended_ticks();
  }
}

void arch_clock_tick(void)
{
  if (software_extended && !atomic_load_explicit(&tsc_selected, memory_order_relaxed) &&
      !--maintenance_ticks) {
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

static uint64_t hpet_ticks(void)
{
  return software_extended ? hpet_extended_ticks() : hpet_direct_ticks();
}

static uint64_t hpet_ticks_to_ns(uint64_t ticks)
{
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

static uint64_t hpet_monotonic_ns(void)
{
  return hpet_ticks_to_ns(hpet_ticks());
}

static uint64_t tsc_read(void)
{
  uint32_t low, high;
  if (tsc_read_kind == TSC_READ_RDTSCP_LFENCE) {
    /* RDTSCP waits for earlier instructions; LFENCE holds later ones back. */
    __asm__ volatile("rdtscp; lfence" : "=a"(low), "=d"(high) : : "rcx", "memory");
  } else {
    uint32_t eax = CPUID_VENDOR, ebx, ecx = 0, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx) : : "memory");
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high) : : "memory");
  }
  return ((uint64_t)high << 32) | low;
}

/* Checks this CPU and prepares its ordered read. Writes DE_CFG only on bare
 * AMD hardware, where the MSR exists from family 10h; guests that do not
 * advertise serializing LFENCE use CPUID ordering instead. */
static enum tsc_read_kind tsc_cpu_read_kind(const char **reason)
{
  uint32_t max_basic, vendor_b, vendor_c, vendor_d;
  cpuid(CPUID_VENDOR, &max_basic, &vendor_b, &vendor_c, &vendor_d);
  uint32_t signature, unused, features_c, features_d;
  cpuid(CPUID_FEATURES, &signature, &unused, &features_c, &features_d);
  uint32_t max_extended, ext_b, ext_c, ext_d;
  cpuid(CPUID_EXTENDED_MAX, &max_extended, &ext_b, &ext_c, &ext_d);
  if (!(features_d & CPUID_FEATURE_TSC) || max_extended < CPUID_POWER_MANAGEMENT) {
    *reason = "has no invariant TSC";
    return TSC_READ_NONE;
  }
  uint32_t power_a, power_b, power_c, power_d;
  cpuid(CPUID_POWER_MANAGEMENT, &power_a, &power_b, &power_c, &power_d);
  if (!(power_d & CPUID_INVARIANT_TSC)) {
    *reason = "has no invariant TSC";
    return TSC_READ_NONE;
  }

  uint32_t extended_a, extended_b, extended_c, extended_d;
  cpuid(CPUID_EXTENDED_FEATURES_LEAF, &extended_a, &extended_b, &extended_c, &extended_d);
  if (!(extended_d & CPUID_FEATURE_RDTSCP)) {
    return TSC_READ_CPUID;
  }
  bool intel = vendor_b == CPUID_INTEL_EBX && vendor_d == CPUID_INTEL_EDX &&
               vendor_c == CPUID_INTEL_ECX;
  bool amd = vendor_b == CPUID_AMD_EBX && vendor_d == CPUID_AMD_EDX && vendor_c == CPUID_AMD_ECX;
  if (intel) {
    return TSC_READ_RDTSCP_LFENCE;
  }
  if (!amd) {
    return TSC_READ_CPUID;
  }
  if (max_extended >= CPUID_EXTENDED_FEATURES_2) {
    uint32_t serial_a, serial_b, serial_c, serial_d;
    cpuid(CPUID_EXTENDED_FEATURES_2, &serial_a, &serial_b, &serial_c, &serial_d);
    if (serial_a & CPUID_LFENCE_ALWAYS_SERIALIZING) {
      return TSC_READ_RDTSCP_LFENCE;
    }
  }
  unsigned family = (signature >> CPUID_FAMILY_SHIFT) & CPUID_FAMILY_MASK;
  if (family == CPUID_FAMILY_MASK) {
    family += (signature >> CPUID_EXTENDED_FAMILY_SHIFT) & CPUID_EXTENDED_FAMILY_MASK;
  }
  if ((features_c & CPUID_FEATURE_HYPERVISOR) || family < AMD_FAMILY_10H) {
    return TSC_READ_CPUID;
  }
  uint64_t de_cfg = read_msr(AMD_DE_CFG);
  if (!(de_cfg & AMD_DE_CFG_LFENCE_SERIALIZE)) {
    write_msr(AMD_DE_CFG, de_cfg | AMD_DE_CFG_LFENCE_SERIALIZE);
    de_cfg = read_msr(AMD_DE_CFG);
  }
  return de_cfg & AMD_DE_CFG_LFENCE_SERIALIZE ? TSC_READ_RDTSCP_LFENCE : TSC_READ_CPUID;
}

static void tsc_reject(size_t cpu, const char *reason)
{
  if (tsc_candidate) {
    tsc_candidate = false;
    tsc_rejected_cpu = cpu;
    tsc_rejection = reason;
  }
}

struct tsc_sample {
  uint64_t tsc, width, hpet;
};

/* The HPET read happened somewhere within the narrowest TSC bracket; its
 * midpoint is at most half the width from the true pairing. */
static struct tsc_sample tsc_sample(void)
{
  struct tsc_sample best = {.width = UINT64_MAX};
  for (unsigned i = 0; i < TSC_BRACKET_ATTEMPTS; ++i) {
    uint64_t before = tsc_read();
    uint64_t hpet = hpet_ticks();
    uint64_t after = tsc_read();
    if (after > before && after - before < best.width) {
      best = (struct tsc_sample){before + (after - before) / 2, after - before, hpet};
    }
  }
  return best;
}

static uint64_t divide_round_up(uint64_t value, uint64_t divisor)
{
  return value / divisor + (value % divisor != 0);
}

/* BSP, IF=0, before APs start: decide the read kind and measure the TSC
 * against the HPET for TSC_CALIBRATION_NS. */
static void tsc_calibrate(void)
{
  const char *reason = NULL;
  tsc_candidate = true;
  tsc_read_kind = tsc_cpu_read_kind(&reason);
  if (tsc_read_kind == TSC_READ_NONE) {
    tsc_reject(0, reason);
    return;
  }

  uint32_t max_basic, unused_b, unused_c, unused_d;
  cpuid(CPUID_VENDOR, &max_basic, &unused_b, &unused_c, &unused_d);
  if (max_basic >= CPUID_TSC_RATIO) {
    uint32_t denominator, numerator, crystal_hz, unused;
    cpuid(CPUID_TSC_RATIO, &denominator, &numerator, &crystal_hz, &unused);
    if (denominator && numerator && crystal_hz) {
      tsc_cpuid_hz = (uint64_t)crystal_hz * numerator / denominator;
    }
  }

  struct tsc_sample start = tsc_sample();
  uint64_t start_ns = hpet_ticks_to_ns(start.hpet);
  while (hpet_monotonic_ns() - start_ns < TSC_CALIBRATION_NS) {
    __asm__ volatile("pause");
  }
  struct tsc_sample end = tsc_sample();
  uint64_t elapsed_ns = hpet_ticks_to_ns(end.hpet) - start_ns;
  if (start.width == UINT64_MAX || end.width == UINT64_MAX || end.tsc <= start.tsc ||
      end.tsc - start.tsc > UINT64_MAX / NANOSECONDS_PER_SECOND) {
    tsc_reject(0, "calibration failed");
    return;
  }
  uint64_t cycles = end.tsc - start.tsc;
  tsc_hz = cycles * NANOSECONDS_PER_SECOND / elapsed_ns;
  /* Half of each bracket in TSC cycles, plus one HPET period, relative to
   * the interval: period_fs / elapsed_ns is that period in ppm. */
  uint64_t uncertainty = start.width / 2 + end.width / 2 + 2;
  tsc_error_ppm = divide_round_up(uncertainty * 1000000, cycles) +
                  divide_round_up(period_fs, elapsed_ns);
  if (tsc_hz < TSC_MIN_HZ) {
    tsc_reject(0, "calibration failed");
  } else if (tsc_error_ppm > TSC_MAX_ERROR_PPM) {
    tsc_reject(0, "calibration too uncertain");
  }
}

/* One BSP/AP pair at a time, both IF=0. Each side repeatedly reads its TSC
 * under a shared lock and fails if the value is below the last one stored.
 * The two counters are never reset: pairs are serialized, so each side of
 * pair k arrives as 2k or 2k + 1 and waits for 2k + 2. */
static _Atomic unsigned warp_arrived, warp_finished;
static _Atomic bool warp_lock, warp_backwards;
static uint64_t warp_last;
static bool ap_tsc_ready;

static void warp_barrier(_Atomic unsigned *counter)
{
  unsigned target = (atomic_fetch_add_explicit(counter, 1, memory_order_acq_rel) / 2 + 1) * 2;
  while (atomic_load_explicit(counter, memory_order_acquire) < target) {
    __asm__ volatile("pause");
  }
}

static void warp_check(void)
{
  warp_barrier(&warp_arrived);
  uint64_t end = tsc_read() + tsc_hz / TSC_WARP_CHECK_DIVISOR;
  for (;;) {
    while (atomic_exchange_explicit(&warp_lock, true, memory_order_acquire)) {
      __asm__ volatile("pause");
    }
    uint64_t previous = warp_last;
    uint64_t now = tsc_read();
    warp_last = now;
    atomic_store_explicit(&warp_lock, false, memory_order_release);
    if (now < previous) {
      atomic_store_explicit(&warp_backwards, true, memory_order_relaxed);
    }
    if (now >= end || atomic_load_explicit(&warp_backwards, memory_order_relaxed)) {
      break;
    }
  }
  warp_barrier(&warp_finished);
}

void arch_clock_ap_prepare(void)
{
  /* tsc_candidate and tsc_read_kind are stable while this AP starts. */
  const char *reason = NULL;
  ap_tsc_ready = false;
  if (!tsc_candidate) {
    return;
  }
  enum tsc_read_kind kind = tsc_cpu_read_kind(&reason);
  ap_tsc_ready = kind != TSC_READ_NONE &&
                 (tsc_read_kind == TSC_READ_CPUID || kind == TSC_READ_RDTSCP_LFENCE);
}

void arch_clock_ap_check(void)
{
  if (tsc_candidate && ap_tsc_ready) {
    warp_check();
  }
}

void arch_clock_bsp_check(size_t ap_index)
{
  if (!tsc_candidate) {
    return;
  }
  if (!ap_tsc_ready) {
    tsc_reject(ap_index, "cannot read its TSC like CPU 0");
    return;
  }
  /* The AP touches these only after both sides pass the arrival barrier. */
  atomic_store_explicit(&warp_backwards, false, memory_order_relaxed);
  warp_last = 0;
  warp_check();
  if (atomic_load_explicit(&warp_backwards, memory_order_relaxed)) {
    tsc_reject(ap_index, "TSC went backwards against CPU 0");
  }
}

void arch_clock_select(void)
{
  size_t cpus = arch_cpu_count();
  if (!tsc_candidate) {
    if (tsc_error_ppm > TSC_MAX_ERROR_PPM) {
      klog("clock: HPET kept: CPU %zu %s (+/-%llu ppm)\n", tsc_rejected_cpu, tsc_rejection,
           (unsigned long long)tsc_error_ppm);
    } else if (tsc_rejection) {
      klog("clock: HPET kept: CPU %zu %s\n", tsc_rejected_cpu, tsc_rejection);
    }
    return;
  }

  /* Read the TSC before the HPET: TSC time then starts at or after any HPET
   * reading another CPU takes before it observes the switch. */
  tsc_base = tsc_read();
  tsc_base_ns = hpet_monotonic_ns();
  tsc_multiplier = (NANOSECONDS_PER_SECOND << TSC_MULTIPLIER_SHIFT) / tsc_hz;
  atomic_store_explicit(&tsc_selected, true, memory_order_release);

  uint64_t khz = tsc_hz / 1000;
  const char *kind = tsc_read_kind == TSC_READ_RDTSCP_LFENCE ? "RDTSCP+LFENCE" : "CPUID+RDTSC";
  if (tsc_cpuid_hz) {
    uint64_t cpuid_khz = tsc_cpuid_hz / 1000;
    klog("clock: TSC selected on %zu CPUs, %llu.%c%c%c MHz calibrated against HPET "
         "(+/-%llu ppm), %s; CPUID 0x15 %llu.%c%c%c MHz\n", cpus,
         (unsigned long long)(khz / 1000), (char)('0' + khz / 100 % 10),
         (char)('0' + khz / 10 % 10), (char)('0' + khz % 10),
         (unsigned long long)tsc_error_ppm, kind,
         (unsigned long long)(cpuid_khz / 1000), (char)('0' + cpuid_khz / 100 % 10),
         (char)('0' + cpuid_khz / 10 % 10), (char)('0' + cpuid_khz % 10));
  } else {
    klog("clock: TSC selected on %zu CPUs, %llu.%c%c%c MHz calibrated against HPET "
         "(+/-%llu ppm), %s\n", cpus,
         (unsigned long long)(khz / 1000), (char)('0' + khz / 100 % 10),
         (char)('0' + khz / 10 % 10), (char)('0' + khz % 10),
         (unsigned long long)tsc_error_ppm, kind);
  }
}

static uint64_t tsc_monotonic_ns(void)
{
  uint64_t now = tsc_read();
  /* Agreement was checked only to the startup bound; never wrap below base. */
  uint64_t delta = now > tsc_base ? now - tsc_base : 0;
  __uint128_t scaled = ((__uint128_t)delta * tsc_multiplier) >> TSC_MULTIPLIER_SHIFT;
  if (scaled > UINT64_MAX - tsc_base_ns) {
    return UINT64_MAX;
  }
  return tsc_base_ns + (uint64_t)scaled;
}

uint64_t arch_monotonic_ns(void)
{
  if (atomic_load_explicit(&tsc_selected, memory_order_acquire)) {
    return tsc_monotonic_ns();
  }
  return hpet_monotonic_ns();
}
