#include <arch/acpi.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/layout.h>
#include <arch/smp.h>
#include <kernel-config.h>
#include <kernel/log.h>
#include <kernel/mm/types.h>
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
#define TSC_CALIBRATION_POLL_LIMIT 10000000
#define TSC_CALIBRATION_WATCHDOG_SECONDS UINT64_C(1)
#define TSC_BRACKET_ATTEMPTS 8
#define TSC_MAX_ERROR_PPM 100
/* CPUID 0x15 wins; a larger HPET disagreement is reported, not acted on. */
#define TSC_CPUID_DISAGREEMENT_PPM 1000
#define TSC_MIN_HZ UINT64_C(100000000)
#define TSC_WARP_CHECK_DIVISOR 500 /* hz / 500: about 2 ms per AP */
#define TSC_MULTIPLIER_SHIFT 32

#define KVM_CPUID_SIGNATURE 0x40000000
#define KVM_CPUID_LIMIT 0x40010000
#define KVM_CPUID_STRIDE 0x100
#define KVM_CPUID_FEATURES_OFFSET 1
#define KVM_SIGNATURE_EBX 0x4b4d564b
#define KVM_SIGNATURE_ECX 0x564b4d56
#define KVM_SIGNATURE_EDX 0x4d
#define KVM_FEATURE_CLOCKSOURCE2 (1u << 3)
#define KVM_FEATURE_STABLE_BIT (1u << 24)
#define KVM_SYSTEM_TIME_NEW 0x4b564d01
#define KVM_SYSTEM_TIME_ENABLE UINT64_C(1)
#define KVM_TIME_STABLE (1u << 0)
#define KVM_TIME_MULTIPLIER_SHIFT 32

/* MSR_KVM_SYSTEM_TIME_NEW's RAM interface, updated under its version word. */
struct kvm_time_info {
  uint32_t version, reserved;
  uint64_t tsc_timestamp, system_time;
  uint32_t multiplier;
  int8_t shift;
  uint8_t flags, padding[2];
};

_Static_assert(sizeof(struct kvm_time_info) == 32 &&
    offsetof(struct kvm_time_info, tsc_timestamp) == 8 &&
    offsetof(struct kvm_time_info, system_time) == 16 &&
    offsetof(struct kvm_time_info, multiplier) == 24 &&
    offsetof(struct kvm_time_info, shift) == 28 &&
    offsetof(struct kvm_time_info, flags) == 29, "KVM time record layout");

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
static uint64_t tsc_hz, tsc_error_ppm;
/* Intel's CPUID 0x15 core crystal and the TSC frequency derived from it;
 * zero when the leaf is absent or incomplete, and always on AMD. */
static uint64_t crystal_hz, tsc_cpuid_hz;
/* The HPET measurement; with CPUID 0x15 it is only a cross-check. */
static uint64_t tsc_measured_hz;
/* On timeout qualified boot readers provisionally use the CPUID TSC. A later
 * CPU rejection is fatal because the HPET cannot be retained. */
static bool hpet_calibration_timed_out;
/* Each AP owns its flag, set with IF=0 after its own read/warp checks and
 * before it enables interrupts while waiting for scheduler startup. */
static bool tsc_bootstrap_ready[ARCH_CPU_LIMIT];
static uint64_t tsc_base, tsc_base_ns, tsc_multiplier;
static _Atomic bool tsc_selected;

/* Separate host-updated records per CPU, resident WB kernel data for the
 * entire registered lifetime, even when partial admission keeps the fallback. */
struct kvm_cpu_clock {
  struct kvm_time_info time;
  enum tsc_read_kind ordering;
  bool ready, stable;
} __attribute__((aligned(64)));

static struct kvm_cpu_clock kvm_clocks[ARCH_CPU_LIMIT]
  __attribute__((aligned(ARCH_PAGE_SIZE)));
static phys_addr_t kvm_clocks_physical;
static bool kvm_available;
static uint64_t kvm_base, kvm_base_ns;
static _Atomic bool kvm_selected;
static _Atomic uint64_t kvm_last_ns;

_Static_assert(__atomic_always_lock_free(sizeof(kvm_last_ns), 0),
    "KVM clock floor must be NMI-safe");

static void tsc_calibrate(void);
static void kvm_prepare(const struct boot_info *boot);
static void kvm_register_cpu(void);
static bool kvm_select(void);
static uint64_t kvm_monotonic_ns(void);

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
  kvm_prepare(boot);
  hpet_physical = acpi_hpet_address(boot);
  if (hpet_physical & (ARCH_PAGE_SIZE - 1)) {
    panic("HPET register block is not page aligned");
  }
}

uint64_t arch_clock_physical_address(void)
{
  return hpet_physical;
}

static bool cpu_is_intel(void)
{
  uint32_t max_basic, vendor_b, vendor_c, vendor_d;
  cpuid(CPUID_VENDOR, &max_basic, &vendor_b, &vendor_c, &vendor_d);
  return vendor_b == CPUID_INTEL_EBX && vendor_d == CPUID_INTEL_EDX &&
         vendor_c == CPUID_INTEL_ECX;
}

/* On Intel, a complete leaf 0x15 gives the core crystal frequency and the
 * TSC/crystal ratio. The local APIC timer runs from that crystal. */
static void read_cpuid_frequencies(void)
{
  uint32_t max_basic, unused_b, unused_c, unused_d;
  cpuid(CPUID_VENDOR, &max_basic, &unused_b, &unused_c, &unused_d);
  if (!cpu_is_intel() || max_basic < CPUID_TSC_RATIO) {
    return;
  }
  uint32_t denominator, numerator, crystal, unused;
  cpuid(CPUID_TSC_RATIO, &denominator, &numerator, &crystal, &unused);
  if (denominator && numerator && crystal) {
    crystal_hz = crystal;
    tsc_cpuid_hz = (uint64_t)crystal * numerator / denominator;
  }
}

uint64_t arch_clock_crystal_hz(void)
{
  return crystal_hz;
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
  kinfo("clock: HPET %u-bit counter, %s, period=%u fs\n", counter_bits,
        software_extended ? "software-extended" : "direct", period_fs);
  if (software_extended) {
    kinfo("clock: HPET maintenance every %u BSP timer ticks; BSP timer frequency %u Hz\n",
          maintenance_ticks, arch_timer_frequency());
  }
  read_cpuid_frequencies();
  tsc_calibrate();
  kvm_register_cpu();
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
  if (software_extended && !atomic_load_explicit(&kvm_selected, memory_order_relaxed) &&
      !atomic_load_explicit(&tsc_selected, memory_order_relaxed)) {
    hpet_extended_ticks();
  }
}

void arch_clock_tick(void)
{
  if (software_extended && !atomic_load_explicit(&kvm_selected, memory_order_relaxed) &&
      !atomic_load_explicit(&tsc_selected, memory_order_relaxed) &&
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

uint64_t arch_clock_hpet_ns(void)
{
  return hpet_monotonic_ns();
}

static void tsc_serialize(void)
{
  uint32_t eax = CPUID_VENDOR, ebx, ecx = 0, edx;
  __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx) : : "memory");
}

static uint64_t tsc_read_ordered(enum tsc_read_kind kind)
{
  uint32_t low, high;
  if (kind == TSC_READ_RDTSCP_LFENCE) {
    /* RDTSCP waits for earlier instructions; LFENCE holds later ones back. */
    __asm__ volatile("rdtscp; lfence" : "=a"(low), "=d"(high) : : "rcx", "memory");
  } else {
    tsc_serialize();
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high) : : "memory");
  }
  return ((uint64_t)high << 32) | low;
}

static uint64_t tsc_read(void)
{
  return tsc_read_ordered(tsc_read_kind);
}

/* Checks this CPU and prepares its ordered read. Writes DE_CFG only on bare
 * AMD hardware, where the MSR exists from family 10h; guests that do not
 * advertise serializing LFENCE use CPUID ordering instead. */
static enum tsc_read_kind tsc_cpu_read_kind(bool require_invariant, const char **reason)
{
  uint32_t max_basic, vendor_b, vendor_c, vendor_d;
  cpuid(CPUID_VENDOR, &max_basic, &vendor_b, &vendor_c, &vendor_d);
  uint32_t signature, unused, features_c, features_d;
  cpuid(CPUID_FEATURES, &signature, &unused, &features_c, &features_d);
  uint32_t max_extended, ext_b, ext_c, ext_d;
  cpuid(CPUID_EXTENDED_MAX, &max_extended, &ext_b, &ext_c, &ext_d);
  if (!(features_d & CPUID_FEATURE_TSC) ||
      (require_invariant && max_extended < CPUID_POWER_MANAGEMENT)) {
    *reason = require_invariant ? "has no invariant TSC" : "has no TSC";
    return TSC_READ_NONE;
  }
  if (require_invariant) {
    uint32_t power_a, power_b, power_c, power_d;
    cpuid(CPUID_POWER_MANAGEMENT, &power_a, &power_b, &power_c, &power_d);
    if (!(power_d & CPUID_INVARIANT_TSC)) {
      *reason = "has no invariant TSC";
      return TSC_READ_NONE;
    }
  }
  if (max_extended < CPUID_EXTENDED_FEATURES_LEAF) {
    return TSC_READ_CPUID;
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

static uint32_t kvm_features(void)
{
  uint32_t a, b, c, d;
  cpuid(CPUID_FEATURES, &a, &b, &c, &d);
  if (!(c & CPUID_FEATURE_HYPERVISOR)) {
    return 0;
  }
  /* Hyper-V enlightenments can move KVM's leaves to the next 0x100 block. */
  for (uint32_t base = KVM_CPUID_SIGNATURE; base < KVM_CPUID_LIMIT;
       base += KVM_CPUID_STRIDE) {
    cpuid(base, &a, &b, &c, &d);
    if (b != KVM_SIGNATURE_EBX || c != KVM_SIGNATURE_ECX || d != KVM_SIGNATURE_EDX) {
      continue;
    }
    uint32_t features = base + KVM_CPUID_FEATURES_OFFSET;
    /* Older KVM returned zero here; a nonzero maximum is an absolute leaf. */
    if (a && a < features) {
      return 0;
    }
    cpuid(features, &a, &b, &c, &d);
    return a;
  }
  return 0;
}

static void kvm_prepare(const struct boot_info *boot)
{
  kvm_available = (kvm_features() & KVM_FEATURE_CLOCKSOURCE2) != 0;
  if (!kvm_available) {
    return;
  }
  uintptr_t address = (uintptr_t)kvm_clocks;
  KASSERT(address >= boot->kernel_virt && address - boot->kernel_virt <= boot->kernel_size &&
      sizeof(kvm_clocks) <= boot->kernel_size - (address - boot->kernel_virt));
  kvm_clocks_physical = boot->kernel_phys + (address - boot->kernel_virt);
}

/* The GDT remains embedded in this CPU's record across root changes and
 * SWAPGS windows. Unlike GS lookup, this is also valid in debugger/NMI entry. */
static struct kvm_cpu_clock *kvm_current_clock(void)
{
  struct descriptor_table_pointer gdtr;
  __asm__ volatile("sgdt %0" : "=m"(gdtr) : : "memory");
  struct cpu_local *cpu = (void *)(uintptr_t)(gdtr.base -
      offsetof(struct cpu_local, descriptors.gdt));
  KASSERT(cpu->index < ARCH_CPU_LIMIT);
  return &kvm_clocks[cpu->index];
}

struct kvm_sample {
  uint64_t tsc, timestamp, system_time;
  uint32_t multiplier;
  int8_t shift;
  uint8_t flags;
};

/* IF=0 pins the vCPU association. The host may change conversion parameters
 * at any time, including when the vCPU moves between host CPUs. */
static struct kvm_sample kvm_sample(struct kvm_cpu_clock *clock)
{
  for (;;) {
    uint32_t version = __atomic_load_n(&clock->time.version, __ATOMIC_ACQUIRE);
    if (version & 1) {
      __asm__ volatile("pause");
      continue;
    }
    struct kvm_sample sample = {
      .timestamp = __atomic_load_n(&clock->time.tsc_timestamp, __ATOMIC_RELAXED),
      .system_time = __atomic_load_n(&clock->time.system_time, __ATOMIC_RELAXED),
      .multiplier = __atomic_load_n(&clock->time.multiplier, __ATOMIC_RELAXED),
      .shift = __atomic_load_n(&clock->time.shift, __ATOMIC_RELAXED),
      .flags = __atomic_load_n(&clock->time.flags, __ATOMIC_RELAXED),
    };
    sample.tsc = tsc_read_ordered(clock->ordering);
    if (clock->ordering == TSC_READ_CPUID) {
      /* This fallback also needs to hold final version validation after RDTSC. */
      tsc_serialize();
    }
    if (version == __atomic_load_n(&clock->time.version, __ATOMIC_ACQUIRE)) {
      return sample;
    }
  }
}

static uint64_t kvm_sample_ns(const struct kvm_sample *sample)
{
  uint64_t delta = sample->tsc > sample->timestamp ? sample->tsc - sample->timestamp : 0;
  int shift = sample->shift;
  if (shift < 0) {
    unsigned right = (unsigned)-shift;
    delta = right >= 64 ? 0 : delta >> right;
  }
  __uint128_t product = (__uint128_t)delta * sample->multiplier;
  __uint128_t scaled;
  if (shift <= 0) {
    scaled = product >> KVM_TIME_MULTIPLIER_SHIFT;
  } else if (shift <= KVM_TIME_MULTIPLIER_SHIFT) {
    scaled = product >> (KVM_TIME_MULTIPLIER_SHIFT - shift);
  } else {
    unsigned left = (unsigned)shift - KVM_TIME_MULTIPLIER_SHIFT;
    if (product && (left >= 64 || product > (UINT64_MAX >> left))) {
      return UINT64_MAX;
    }
    scaled = product ? (uint64_t)product << left : 0;
  }
  if (scaled > UINT64_MAX - sample->system_time) {
    return UINT64_MAX;
  }
  return sample->system_time + (uint64_t)scaled;
}

static void kvm_register_cpu(void)
{
  if (!kvm_available || !(kvm_features() & KVM_FEATURE_CLOCKSOURCE2)) {
    return;
  }
  const char *reason = NULL;
  enum tsc_read_kind kind = tsc_cpu_read_kind(false, &reason);
  if (kind == TSC_READ_NONE) {
    return;
  }
  struct kvm_cpu_clock *clock = kvm_current_clock();
  clock->ordering = kind;
  phys_addr_t physical = kvm_clocks_physical +
      ((uintptr_t)&clock->time - (uintptr_t)kvm_clocks);
  write_msr(KVM_SYSTEM_TIME_NEW, physical | KVM_SYSTEM_TIME_ENABLE);
  struct kvm_sample sample = kvm_sample(clock);
  clock->ready = sample.multiplier && kvm_sample_ns(&sample) != UINT64_MAX;
  clock->stable = (kvm_features() & KVM_FEATURE_STABLE_BIT) && (sample.flags & KVM_TIME_STABLE);
}

/* Bootstrap fallback reads join the same floor before selection: an AP can
 * finish an old-source read after the BSP takes the common epoch pair. */
static uint64_t kvm_floor(uint64_t now)
{
  uint64_t previous = atomic_load_explicit(&kvm_last_ns, memory_order_relaxed);
  /* Always retain the floor, including when the host changes its stable flag.
   * A nested NMI reader never waits for its interrupted reader to finish. */
  while (now > previous && !atomic_compare_exchange_weak_explicit(&kvm_last_ns,
      &previous, now, memory_order_relaxed, memory_order_relaxed)) {
  }
  return now > previous ? now : previous;
}

static bool kvm_select(void)
{
  if (!kvm_available) {
    return false;
  }
  size_t stable = 0, cpus = arch_cpu_count();
  for (size_t i = 0; i < cpus; ++i) {
    if (!kvm_clocks[i].ready) {
      kdebug("clock: KVM CPU %zu time record unavailable; retaining native fallback\n", i);
      return false;
    }
    stable += kvm_clocks[i].stable;
  }
  struct kvm_sample sample = kvm_sample(kvm_current_clock());
  kvm_base = kvm_sample_ns(&sample);
  if (!sample.multiplier || kvm_base == UINT64_MAX) {
    return false;
  }
  /* Sample the new source first so the old epoch does not move backwards at
   * publication. Every CPU uses this common pair, never an individual offset. */
  kvm_base_ns = arch_monotonic_ns();
  atomic_store_explicit(&kvm_selected, true, memory_order_release);
  kinfo("clock: KVM selected on %zu CPUs, shared monotonic floor; "
        "stable flag %zu/%zu at registration\n", cpus, stable, cpus);
  return true;
}

uint64_t arch_tsc_cycles_ns(uint64_t cycles)
{
  KASSERT(!cpu_interrupts_enabled());
  if (tsc_hz) {
    /* Whole seconds, then the remainder: no 128-bit division in the kernel.
     * The remainder product fits while the rate is below 18 GHz. */
    return cycles / tsc_hz * 1000000000u + cycles % tsc_hz * 1000000000u / tsc_hz;
  }
  if (!atomic_load_explicit(&kvm_selected, memory_order_acquire)) {
    return 0;
  }
  /* The host's current TSC-to-time scale, applied to a plain interval. */
  struct kvm_sample sample = kvm_sample(kvm_current_clock());
  sample.tsc = cycles;
  sample.timestamp = 0;
  sample.system_time = 0;
  return kvm_sample_ns(&sample);
}

static uint64_t kvm_monotonic_ns(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct kvm_sample sample = kvm_sample(kvm_current_clock());
  uint64_t raw = kvm_sample_ns(&sample);
  /* An unusable host tuple must not permanently saturate the shared floor.
   * Keep the last valid time; a later valid host update can advance it again. */
  uint64_t delta = raw > kvm_base ? raw - kvm_base : 0;
  uint64_t result;
  if (!sample.multiplier || raw == UINT64_MAX || delta >= UINT64_MAX - kvm_base_ns) {
    result = atomic_load_explicit(&kvm_last_ns, memory_order_relaxed);
  } else {
    result = kvm_floor(kvm_base_ns + delta);
  }
  cpu_restore_interrupts(flags);
  return result;
}

static void tsc_reject(size_t cpu, const char *reason)
{
  if (hpet_calibration_timed_out) {
    panic("clock unusable: HPET calibration timed out and CPU %zu %s", cpu, reason);
  }
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

/* The watchdog uses only an independently enumerated, qualified TSC. The
 * poll cap also terminates when no frequency is known or the TSC stops; it
 * bounds iterations rather than promising a wall-time limit in that case. */
static bool hpet_wait_interval(uint64_t start_ns, uint64_t interval_ns)
{
  bool watchdog = tsc_read_kind != TSC_READ_NONE && tsc_cpuid_hz;
  uint64_t start_tsc = watchdog ? tsc_read() : 0;
  uint64_t watchdog_cycles = tsc_cpuid_hz * TSC_CALIBRATION_WATCHDOG_SECONDS;
  for (unsigned poll = 0; poll < TSC_CALIBRATION_POLL_LIMIT; ++poll) {
    uint64_t now_ns = hpet_monotonic_ns();
    if (now_ns < start_ns) {
      return false;
    }
    if (now_ns - start_ns >= interval_ns) {
      return true;
    }
    if (watchdog) {
      uint64_t now_tsc = tsc_read();
      if (now_tsc < start_tsc || now_tsc - start_tsc >= watchdog_cycles) {
        return false;
      }
    }
    __asm__ volatile("pause");
  }
  return false;
}

/* BSP, IF=0, before APs start: decide the read kind and measure the TSC
 * against the HPET for TSC_CALIBRATION_NS. With CPUID 0x15, its frequency
 * is used and the measurement only cross-checks it. */
static void tsc_calibrate(void)
{
  const char *reason = NULL;
  tsc_candidate = true;
  tsc_read_kind = tsc_cpu_read_kind(true, &reason);
  if (tsc_read_kind == TSC_READ_NONE) {
    if (!hpet_wait_interval(hpet_monotonic_ns(), 1)) {
      panic("clock unusable: HPET did not advance and CPU 0 %s", reason);
    }
    tsc_reject(0, reason);
    return;
  }

  struct tsc_sample start = tsc_sample();
  uint64_t start_ns = hpet_ticks_to_ns(start.hpet);
  if (!hpet_wait_interval(start_ns, TSC_CALIBRATION_NS)) {
    hpet_calibration_timed_out = true;
    if (!tsc_cpuid_hz) {
      panic("clock unusable: HPET calibration timed out without a qualified "
            "CPUID 0x15 TSC frequency");
    }
    tsc_hz = tsc_cpuid_hz;
    tsc_base = tsc_read();
    tsc_base_ns = hpet_monotonic_ns();
    tsc_multiplier = (NANOSECONDS_PER_SECOND << TSC_MULTIPLIER_SHIFT) / tsc_hz;
    if (tsc_read() <= tsc_base) {
      panic("clock unusable: HPET calibration timed out and CPU 0 TSC did not advance");
    }
    kwarn("clock: HPET calibration timed out; BSP boot uses CPUID 0x15 TSC "
          "pending all-CPU qualification\n");
    return;
  }
  struct tsc_sample end = tsc_sample();
  uint64_t elapsed_ns = hpet_ticks_to_ns(end.hpet) - start_ns;
  bool measured = start.width != UINT64_MAX && end.width != UINT64_MAX &&
      elapsed_ns >= TSC_CALIBRATION_NS &&
      end.tsc > start.tsc && end.tsc - start.tsc <= UINT64_MAX / NANOSECONDS_PER_SECOND;
  if (measured) {
    uint64_t cycles = end.tsc - start.tsc;
    tsc_measured_hz = cycles * NANOSECONDS_PER_SECOND / elapsed_ns;
    /* Half of each bracket in TSC cycles, plus one HPET period, relative to
     * the interval: period_fs / elapsed_ns is that period in ppm. */
    uint64_t uncertainty = start.width / 2 + end.width / 2 + 2;
    tsc_error_ppm = divide_round_up(uncertainty * 1000000, cycles) +
                    divide_round_up(period_fs, elapsed_ns);
  }

  if (tsc_cpuid_hz) {
    tsc_hz = tsc_cpuid_hz;
    if (!measured) {
      kwarn("clock: TSC HPET cross-check failed; using CPUID 0x15\n");
      return;
    }
    uint64_t difference = tsc_measured_hz > tsc_cpuid_hz ? tsc_measured_hz - tsc_cpuid_hz :
                          tsc_cpuid_hz - tsc_measured_hz;
    uint64_t disagreement_ppm = divide_round_up(difference * 1000000, tsc_cpuid_hz);
    if (disagreement_ppm > TSC_CPUID_DISAGREEMENT_PPM) {
      kwarn("clock: TSC measured against HPET at %lu kHz disagrees with CPUID 0x15 %lu kHz "
            "by %lu ppm; using CPUID 0x15\n", tsc_measured_hz / 1000, tsc_cpuid_hz / 1000,
            disagreement_ppm);
    }
    return;
  }

  if (!measured) {
    tsc_reject(0, "calibration failed");
    return;
  }
  tsc_hz = tsc_measured_hz;
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
  kvm_register_cpu();
  /* tsc_candidate and tsc_read_kind are stable while this AP starts. */
  const char *reason = NULL;
  ap_tsc_ready = false;
  if (!tsc_candidate) {
    return;
  }
  enum tsc_read_kind kind = tsc_cpu_read_kind(true, &reason);
  ap_tsc_ready = kind != TSC_READ_NONE &&
                 (tsc_read_kind == TSC_READ_CPUID || kind == TSC_READ_RDTSCP_LFENCE);
}

void arch_clock_ap_check(void)
{
  if (tsc_candidate && ap_tsc_ready) {
    warp_check();
    if (!atomic_load_explicit(&warp_backwards, memory_order_relaxed)) {
      tsc_bootstrap_ready[arch_cpu_index()] = true;
    }
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
  if (kvm_select()) {
    return;
  }
  size_t cpus = arch_cpu_count();
  if (!tsc_candidate) {
    if (!tsc_cpuid_hz && tsc_error_ppm > TSC_MAX_ERROR_PPM) {
      kinfo("clock: HPET kept: CPU %zu %s (+/-%llu ppm)\n", tsc_rejected_cpu, tsc_rejection,
            (unsigned long long)tsc_error_ppm);
    } else if (tsc_rejection) {
      kinfo("clock: HPET kept: CPU %zu %s\n", tsc_rejected_cpu, tsc_rejection);
    }
    return;
  }

  if (!hpet_calibration_timed_out) {
    /* Read the TSC before the HPET: TSC time then starts at or after any HPET
     * reading another CPU takes before it observes the switch. */
    tsc_base = tsc_read();
    tsc_base_ns = hpet_monotonic_ns();
    tsc_multiplier = (NANOSECONDS_PER_SECOND << TSC_MULTIPLIER_SHIFT) / tsc_hz;
  }
  /* Timeout recovery keeps the advancing epoch already used by BSP boot
   * readers; rebasing to the failed HPET would step their time backwards. */
  atomic_store_explicit(&tsc_selected, true, memory_order_release);

  const char *kind = tsc_read_kind == TSC_READ_RDTSCP_LFENCE ? "RDTSCP+LFENCE" : "CPUID+RDTSC";
  uint64_t khz = tsc_hz / 1000;
  if (tsc_cpuid_hz && !tsc_measured_hz) {
    kinfo("clock: TSC selected on %zu CPUs, %lu.%03lu MHz from CPUID 0x15, %s; "
          "no HPET cross-check\n", cpus, khz / 1000, khz % 1000, kind);
  } else if (tsc_cpuid_hz) {
    uint64_t measured_khz = tsc_measured_hz / 1000;
    kinfo("clock: TSC selected on %zu CPUs, %lu.%03lu MHz from CPUID 0x15, %s; "
          "HPET measured %lu.%03lu MHz (+/-%lu ppm)\n", cpus, khz / 1000, khz % 1000, kind,
          measured_khz / 1000, measured_khz % 1000, tsc_error_ppm);
  } else {
    kinfo("clock: TSC selected on %zu CPUs, %lu.%03lu MHz calibrated against HPET "
          "(+/-%lu ppm), %s\n", cpus, khz / 1000, khz % 1000, tsc_error_ppm, kind);
  }
}

static uint64_t tsc_monotonic_ns(void)
{
  uint64_t now = tsc_read();
  /* Startup checks cannot guarantee future agreement; never wrap below base. */
  uint64_t delta = now > tsc_base ? now - tsc_base : 0;
  __uint128_t scaled = ((__uint128_t)delta * tsc_multiplier) >> TSC_MULTIPLIER_SHIFT;
  if (scaled > UINT64_MAX - tsc_base_ns) {
    return UINT64_MAX;
  }
  return tsc_base_ns + (uint64_t)scaled;
}

uint64_t arch_monotonic_ns(void)
{
  if (atomic_load_explicit(&kvm_selected, memory_order_acquire)) {
    return kvm_monotonic_ns();
  }
  uint64_t now;
  if (atomic_load_explicit(&tsc_selected, memory_order_acquire)) {
    now = tsc_monotonic_ns();
  } else if (hpet_calibration_timed_out) {
    size_t cpu = arch_cpu_index();
    if (cpu != 0 && !tsc_bootstrap_ready[cpu]) {
      panic("clock unusable: AP read before its TSC qualification");
    }
    now = tsc_monotonic_ns();
  } else {
    now = hpet_monotonic_ns();
  }
  return kvm_available ? kvm_floor(now) : now;
}

const char *arch_clock_source(void)
{
  if (atomic_load_explicit(&kvm_selected, memory_order_acquire)) {
    return "KVM";
  }
  return atomic_load_explicit(&tsc_selected, memory_order_acquire) || hpet_calibration_timed_out ?
      "TSC" : "HPET";
}
