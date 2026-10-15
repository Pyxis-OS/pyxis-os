#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/layout.h>
#include <kernel/log.h>
#include <kernel/panic.h>

#define CPUID_FEATURE_APIC (1u << 9)
#define APIC_BASE_ENABLED (UINT64_C(1) << 11)
#define APIC_BASE_ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define X2APIC_MSR_BASE 0x800
#define X2APIC_REGISTER_SHIFT 4
#define X2APIC_ICR_DESTINATION_SHIFT 32

#define APIC_ID 0x020
#define APIC_ID_SHIFT 24
#define APIC_MSI_ADDRESS UINT32_C(0xfee00000)
#define APIC_MSI_DESTINATION_SHIFT 12
#define APIC_VERSION 0x030
#define APIC_TASK_PRIORITY 0x080
#define APIC_EOI 0x0b0
#define APIC_IRR 0x200
#define APIC_REGISTER_STRIDE 0x10
#define APIC_VECTORS_PER_REGISTER 32u
#define APIC_SPURIOUS 0x0f0
#define APIC_ICR_LOW 0x300
#define APIC_ICR_HIGH 0x310
#define APIC_ICR_DESTINATION_SHIFT 24
#define APIC_ICR_SEND_PENDING (1u << 12)
#define APIC_ICR_DELIVERY_NMI (4u << 8)
#define APIC_ICR_POLL_LIMIT 1000000u
#define APIC_LVT_CMCI 0x2f0
#define APIC_LVT_TIMER 0x320
#define APIC_LVT_THERMAL 0x330
#define APIC_LVT_PERFORMANCE 0x340
#define APIC_LVT_LINT0 0x350
#define APIC_LVT_LINT1 0x360
#define APIC_LVT_ERROR 0x370
#define APIC_TIMER_INITIAL 0x380
#define APIC_TIMER_CURRENT 0x390
#define APIC_TIMER_DIVIDE 0x3e0
#define APIC_MAX_LVT_SHIFT 16
#define APIC_MAX_LVT_MASK 0xff
#define APIC_SOFTWARE_ENABLED (1u << 8)
#define APIC_LVT_MASKED (1u << 16)
#define APIC_TIMER_PERIODIC (1u << 17)
#define APIC_DIVIDE_BY_16 0x3

#define PIC_MASTER_MASK 0x21
#define PIC_SLAVE_MASK 0xa1
#define PIC_MASK_ALL 0xff

#define APIC_TIMER_DIVISOR 16u /* APIC_DIVIDE_BY_16 */
#define TIMER_FREQUENCY 120u
#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)
#define TIMER_PERIOD_NS ((NANOSECONDS_PER_SECOND + TIMER_FREQUENCY - 1) / TIMER_FREQUENCY)
#define TIMER_CALIBRATION_NS UINT64_C(50000000)
/* Half the counter: far beyond the calibration interval at any real rate. */
#define TIMER_CALIBRATION_COUNT_LIMIT (UINT32_MAX / 2)

/* Published before AP startup; the mode never changes while Caelum runs. */
static bool x2apic, prepared;
/* The local timer's rate after the divider, and its count for one periodic
 * tick. Set once by the BSP before AP startup; every CPU's timer shares the
 * crystal or bus clock it measures. */
static uint64_t timer_hz;
static uint32_t timer_period_count;

uint32_t arch_timer_frequency(void)
{
  return TIMER_FREQUENCY;
}

struct apic_msi_message apic_bsp_msi_message(uint8_t vector)
{
  uint32_t destination = cpu_bsp()->lapic_id;
  if (destination > UINT8_MAX) {
    panic("MSI target APIC ID %u exceeds 255; interrupt remapping is required", destination);
  }
  KASSERT(vector >= APIC_TIMER_VECTOR && vector < APIC_SPURIOUS_VECTOR);

  /* Address bits 19:12 select the physical APIC. Zero mode/delivery/trigger
   * fields mean physical destination, fixed delivery and edge triggering.
   * This is an interrupt message address, not the CPU's APIC MMIO base. */
  return (struct apic_msi_message){
    .address_low = APIC_MSI_ADDRESS | (destination << APIC_MSI_DESTINATION_SHIFT),
    .address_high = 0,
    .data = vector,
  };
}

static uint32_t apic_read(unsigned offset)
{
  if (x2apic) {
    return (uint32_t)read_msr(X2APIC_MSR_BASE + (offset >> X2APIC_REGISTER_SHIFT));
  }
  return *(volatile uint32_t *)(APIC_BASE + offset);
}

bool apic_vector_requested(uint8_t vector)
{
  uint32_t irr = apic_read(APIC_IRR + vector / APIC_VECTORS_PER_REGISTER * APIC_REGISTER_STRIDE);
  return irr & (UINT32_C(1) << (vector % APIC_VECTORS_PER_REGISTER));
}

static void apic_write(unsigned offset, uint32_t value)
{
  if (x2apic) {
    write_msr(X2APIC_MSR_BASE + (offset >> X2APIC_REGISTER_SHIFT), value);
    return;
  }
  *(volatile uint32_t *)(APIC_BASE + offset) = value;
}

void apic_prepare(bool boot_x2apic)
{
  KASSERT(!prepared);
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
  if (!(edx & CPUID_FEATURE_APIC)) {
    panic("CPU lacks a local APIC");
  }

  uint64_t base = read_msr(IA32_APIC_BASE);
  if (boot_x2apic != !!(ecx & CPUID_FEATURE_X2APIC)) {
    panic("boot local APIC mode disagrees with CPU x2APIC support");
  }
  if (!(base & APIC_BASE_ENABLED) || boot_x2apic != !!(base & APIC_BASE_X2APIC)) {
    panic("boot local APIC mode disagrees with hardware");
  }
  x2apic = boot_x2apic;
  prepared = true;
}

uint64_t apic_physical_address(void)
{
  KASSERT(prepared);
  return x2apic ? 0 : read_msr(IA32_APIC_BASE) & APIC_BASE_ADDRESS_MASK;
}

/* BSP only: one HPET measurement, used where CPUID 0x15 does not give the
 * crystal (AMD). The timer counts down at divide-by-16 throughout. */
static uint64_t measure_timer_hz(void)
{
  apic_write(APIC_TIMER_INITIAL, UINT32_MAX);
  uint32_t start_count = apic_read(APIC_TIMER_CURRENT);
  uint64_t start_ns = arch_clock_hpet_ns();
  uint64_t elapsed_ns = 0;
  uint32_t end_count;
  do {
    __asm__ volatile("pause");
    end_count = apic_read(APIC_TIMER_CURRENT);
    elapsed_ns = arch_clock_hpet_ns() - start_ns;
  } while (elapsed_ns < TIMER_CALIBRATION_NS &&
           start_count - end_count < TIMER_CALIBRATION_COUNT_LIMIT);
  apic_write(APIC_TIMER_INITIAL, 0);

  uint32_t counts = start_count - end_count;
  if (elapsed_ns < TIMER_CALIBRATION_NS || !counts) {
    panic("cannot calibrate the local APIC timer against the HPET");
  }
  return (uint64_t)counts * NANOSECONDS_PER_SECOND / elapsed_ns;
}

/* Rounded up, so an armed deadline never fires early. ns is at most
 * TIMER_PERIOD_NS, which keeps the product far inside uint64_t. */
static uint64_t timer_counts(uint64_t ns)
{
  uint64_t product = timer_hz * ns;
  return product / NANOSECONDS_PER_SECOND + (product % NANOSECONDS_PER_SECOND != 0);
}

static void calibrate_timer(void)
{
  uint64_t crystal = arch_clock_crystal_hz();
  const char *source = "from the CPUID 0x15 crystal";
  if (crystal) {
    timer_hz = crystal / APIC_TIMER_DIVISOR;
  } else {
    timer_hz = measure_timer_hz();
    source = "measured against the HPET";
  }
  uint64_t period_count = timer_counts(TIMER_PERIOD_NS);
  if (!period_count || period_count > UINT32_MAX) {
    panic("unusable local APIC timer rate %lu Hz", timer_hz);
  }
  timer_period_count = (uint32_t)period_count;
  kinfo("x86_64: %s local APIC timer %lu Hz %s, %u counts per %lu ns\n",
        x2apic ? "x2APIC" : "xAPIC", timer_hz, source, timer_period_count, TIMER_PERIOD_NS);
}

void apic_init(void)
{
  if (cpu_current() == cpu_bsp()) {
    outb(PIC_MASTER_MASK, PIC_MASK_ALL);
    outb(PIC_SLAVE_MASK, PIC_MASK_ALL);
  }
  uint64_t base = read_msr(IA32_APIC_BASE);
  if (!prepared || !(base & APIC_BASE_ENABLED) || x2apic != !!(base & APIC_BASE_X2APIC)) {
    panic("CPU local APIC mode differs from the selected boot mode");
  }

  unsigned max_lvt = (apic_read(APIC_VERSION) >> APIC_MAX_LVT_SHIFT) & APIC_MAX_LVT_MASK;
  if (max_lvt < 5) {
    panic("unsupported local APIC version");
  }

  apic_write(APIC_LVT_TIMER, APIC_LVT_MASKED | APIC_TIMER_VECTOR);
  apic_write(APIC_LVT_THERMAL, APIC_LVT_MASKED);
  apic_write(APIC_LVT_PERFORMANCE, APIC_LVT_MASKED);
  apic_write(APIC_LVT_LINT0, APIC_LVT_MASKED);
  apic_write(APIC_LVT_LINT1, APIC_LVT_MASKED);
  apic_write(APIC_LVT_ERROR, APIC_LVT_MASKED);
  if (max_lvt >= 6) {
    apic_write(APIC_LVT_CMCI, APIC_LVT_MASKED);
  }
  apic_write(APIC_TASK_PRIORITY, 0);
  apic_write(APIC_SPURIOUS, APIC_SOFTWARE_ENABLED | APIC_SPURIOUS_VECTOR);
  apic_write(APIC_TIMER_DIVIDE, APIC_DIVIDE_BY_16);

  if (cpu_current() == cpu_bsp()) {
    calibrate_timer();
  }
  cpu_current()->timer_count = timer_period_count;
}

void apic_timer_start(void)
{
  uint32_t timer_count = cpu_current()->timer_count;
  KASSERT(timer_count);
  apic_write(APIC_LVT_TIMER, APIC_TIMER_PERIODIC | APIC_TIMER_VECTOR);
  apic_write(APIC_TIMER_INITIAL, timer_count);
}

void arch_timer_deadline_start(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct cpu_local *cpu = cpu_current();
  KASSERT(cpu->timer_count && !cpu->timer_deadline_started);
  uint64_t now = arch_monotonic_ns();
  cpu->timer_preempt_deadline = TIMER_PERIOD_NS > UINT64_MAX - now ? UINT64_MAX :
                                now + TIMER_PERIOD_NS;
  cpu->timer_deadline_started = true;
  apic_write(APIC_LVT_TIMER, APIC_TIMER_VECTOR);
  arch_timer_arm(UINT64_MAX, now);
}

static uint64_t timer_target(const struct cpu_local *cpu, uint64_t deadline)
{
  return deadline < cpu->timer_preempt_deadline ? deadline : cpu->timer_preempt_deadline;
}

bool arch_timer_arm_needed(uint64_t deadline)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  const struct cpu_local *cpu = cpu_current();
  if (!cpu->timer_deadline_started) {
    return false;
  }
  return !cpu->timer_armed || timer_target(cpu, deadline) < cpu->timer_armed_target;
}

void arch_timer_arm(uint64_t deadline, uint64_t now)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct cpu_local *cpu = cpu_current();
  if (!cpu->timer_deadline_started) {
    return;
  }

  uint64_t next = timer_target(cpu, deadline);
  uint64_t remaining = next > now ? next - now : 1;
  if (remaining > TIMER_PERIOD_NS || now == UINT64_MAX) {
    remaining = TIMER_PERIOD_NS;
  }
  uint64_t count = timer_counts(remaining);
  if (!count) {
    count = 1;
  } else if (count > UINT32_MAX) {
    count = UINT32_MAX;
  }
  apic_write(APIC_TIMER_INITIAL, (uint32_t)count);
  /* A clamped countdown fires before next; record when it actually aims. */
  cpu->timer_armed_target = remaining > UINT64_MAX - now ? UINT64_MAX : now + remaining;
  cpu->timer_armed = true;
}

uint64_t arch_timer_interrupt(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct cpu_local *cpu = cpu_current();
  uint64_t now = arch_monotonic_ns();
  if (!cpu->timer_deadline_started) {
    if (cpu == cpu_bsp()) {
      arch_clock_tick();
    }
    return now;
  }

  cpu->timer_armed = false;
  if (now < cpu->timer_preempt_deadline) {
    return now;
  }
  uint64_t periods = (now - cpu->timer_preempt_deadline) / TIMER_PERIOD_NS + 1;
  if (periods > (UINT64_MAX - cpu->timer_preempt_deadline) / TIMER_PERIOD_NS) {
    cpu->timer_preempt_deadline = UINT64_MAX;
  } else {
    cpu->timer_preempt_deadline += periods * TIMER_PERIOD_NS;
  }
  if (cpu == cpu_bsp()) {
    arch_clock_tick();
  }
  return now;
}

uint32_t apic_timer_remaining(void)
{
  return apic_read(APIC_TIMER_CURRENT);
}

uint32_t apic_id(void)
{
  uint32_t id = apic_read(APIC_ID);
  return x2apic ? id : id >> APIC_ID_SHIFT;
}

void apic_end_interrupt(void)
{
  apic_write(APIC_EOI, 0);
}

static bool try_send_ipi(uint32_t destination, uint32_t command)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(destination != UINT32_MAX);
  if (x2apic) {
    /* x2APIC WRMSR is not serializing. Publish queues/TLB/debug state before
     * notification; MFENCE alone need not order the subsequent WRMSR. */
    __asm__ volatile("mfence; lfence" : : : "memory");
    write_msr(X2APIC_MSR_BASE + (APIC_ICR_LOW >> X2APIC_REGISTER_SHIFT),
        ((uint64_t)destination << X2APIC_ICR_DESTINATION_SHIFT) | command);
    return true;
  }
  KASSERT(destination <= UINT8_MAX);
  if (apic_read(APIC_ICR_LOW) & APIC_ICR_SEND_PENDING) {
    return false;
  }
  apic_write(APIC_ICR_HIGH, destination << APIC_ICR_DESTINATION_SHIFT);
  apic_write(APIC_ICR_LOW, command);
  return true;
}

void apic_send_reschedule(uint32_t destination)
{
  unsigned remaining = APIC_ICR_POLL_LIMIT;
  while (!try_send_ipi(destination, APIC_RESCHEDULE_VECTOR)) {
    if (!--remaining) {
      panic("local APIC IPI delivery stalled");
    }
    __asm__ volatile("pause");
  }
}

bool apic_try_send_tlb_flush(uint32_t destination)
{
  return try_send_ipi(destination, APIC_TLB_FLUSH_VECTOR);
}

bool apic_try_send_nmi(uint32_t destination)
{
  return try_send_ipi(destination, APIC_ICR_DELIVERY_NMI);
}
