#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/layout.h>
#include <kernel/log.h>
#include <kernel/panic.h>

#define CPUID_FEATURE_APIC (1u << 9)
#define IA32_APIC_BASE 0x1b
#define APIC_BASE_ENABLED (UINT64_C(1) << 11)
#define APIC_BASE_X2APIC (UINT64_C(1) << 10)
#define APIC_BASE_ADDRESS_MASK UINT64_C(0x000ffffffffff000)

#define APIC_ID 0x020
#define APIC_ID_SHIFT 24
#define APIC_MSI_ADDRESS UINT32_C(0xfee00000)
#define APIC_MSI_DESTINATION_SHIFT 12
#define APIC_VERSION 0x030
#define APIC_TASK_PRIORITY 0x080
#define APIC_EOI 0x0b0
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

#define PIT_CHANNEL2_DATA 0x42
#define PIT_COMMAND 0x43
#define PIT_CHANNEL2_SELECT (2u << 6)
#define PIT_ACCESS_LO_HI (3u << 4)
#define PIT_MODE_TERMINAL_COUNT 0
#define PIT_SPEAKER_CONTROL 0x61
#define PIT_CHANNEL2_GATE (1u << 0)
#define PIT_SPEAKER_ENABLE (1u << 1)
#define PIT_CHANNEL2_OUTPUT (1u << 5)
#define PIT_FREQUENCY 1193182u
#define TIMER_FREQUENCY 120u
#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)
#define TIMER_PERIOD_NS ((NANOSECONDS_PER_SECOND + TIMER_FREQUENCY - 1) / TIMER_FREQUENCY)
#define PIT_CALIBRATION_COUNT ((PIT_FREQUENCY + TIMER_FREQUENCY - 1) / TIMER_FREQUENCY)
#define PIT_CALIBRATION_DENOMINATOR ((uint64_t)PIT_CALIBRATION_COUNT * NANOSECONDS_PER_SECOND)
#define PIT_POLL_LIMIT 10000000u
#define PIT_CLOCK_POLL_INTERVAL 1024u

uint32_t arch_timer_frequency(void)
{
  return TIMER_FREQUENCY;
}

struct apic_msi_message apic_bsp_msi_message(uint8_t vector)
{
  uint32_t destination = cpu_bsp()->lapic_id;
  KASSERT(destination <= UINT8_MAX);
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
  return *(volatile uint32_t *)(APIC_BASE + offset);
}

static void apic_write(unsigned offset, uint32_t value)
{
  *(volatile uint32_t *)(APIC_BASE + offset) = value;
}

uint64_t apic_physical_address(void)
{
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
  if (!(edx & CPUID_FEATURE_APIC)) {
    panic("CPU lacks a local APIC");
  }

  uint64_t base = read_msr(IA32_APIC_BASE);
  if (base & APIC_BASE_X2APIC) {
    panic("x2APIC boot mode is not supported");
  }
  return base & APIC_BASE_ADDRESS_MASK;
}

static uint32_t calibrate_timer(void)
{
  /* Channel 2 supplies a polling interval without enabling the PIC or speaker.
   * The local timer counts down at divide-by-16 throughout that interval. */
  uint8_t speaker = inb(PIT_SPEAKER_CONTROL);
  uint8_t stopped = speaker & ~(PIT_CHANNEL2_GATE | PIT_SPEAKER_ENABLE);
  outb(PIT_SPEAKER_CONTROL, stopped);
  outb(PIT_COMMAND, PIT_CHANNEL2_SELECT | PIT_ACCESS_LO_HI | PIT_MODE_TERMINAL_COUNT);
  outb(PIT_CHANNEL2_DATA, (uint8_t)PIT_CALIBRATION_COUNT);
  outb(PIT_CHANNEL2_DATA, (uint8_t)(PIT_CALIBRATION_COUNT >> 8));

  apic_write(APIC_TIMER_INITIAL, UINT32_MAX);
  outb(PIT_SPEAKER_CONTROL, stopped | PIT_CHANNEL2_GATE);
  unsigned remaining = PIT_POLL_LIMIT;
  while (!(inb(PIT_SPEAKER_CONTROL) & PIT_CHANNEL2_OUTPUT) && remaining) {
    if (!(remaining % PIT_CLOCK_POLL_INTERVAL)) {
      arch_clock_maintain();
    }
    --remaining;
  }
  uint32_t elapsed = UINT32_MAX - apic_read(APIC_TIMER_CURRENT);
  apic_write(APIC_TIMER_INITIAL, 0);
  outb(PIT_SPEAKER_CONTROL, stopped);

  if (!remaining || !elapsed || elapsed == UINT32_MAX) {
    panic("cannot calibrate the local APIC timer");
  }
  return elapsed;
}

void apic_init(void)
{
  if (cpu_current() == cpu_bsp()) {
    outb(PIC_MASTER_MASK, PIC_MASK_ALL);
    outb(PIC_SLAVE_MASK, PIC_MASK_ALL);
  }
  write_msr(IA32_APIC_BASE, read_msr(IA32_APIC_BASE) | APIC_BASE_ENABLED);

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

  cpu_current()->timer_count = calibrate_timer();
  if (cpu_current() == cpu_bsp()) {
    klog("x86_64: local APIC timer calibrated, %u counts per ~8.33 ms\n",
         cpu_current()->timer_count);
  }
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
  /* The nominal occasion bounds this product. Splitting before multiplying by
   * PIT_FREQUENCY keeps both products in uint64_t, including the rounding add.
   * Use the PIT interval itself, rather than treating it as exactly 1/120 s. */
  uint64_t product = (uint64_t)cpu->timer_count * remaining;
  uint64_t count = (product / PIT_CALIBRATION_DENOMINATOR) * PIT_FREQUENCY;
  uint64_t fraction = (product % PIT_CALIBRATION_DENOMINATOR) * PIT_FREQUENCY;
  count += (fraction + PIT_CALIBRATION_DENOMINATOR - 1) / PIT_CALIBRATION_DENOMINATOR;
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
  return apic_read(APIC_ID) >> APIC_ID_SHIFT;
}

void apic_end_interrupt(void)
{
  apic_write(APIC_EOI, 0);
}

void apic_send_reschedule(uint32_t destination)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(destination <= UINT8_MAX);

  unsigned remaining = APIC_ICR_POLL_LIMIT;
  while (apic_read(APIC_ICR_LOW) & APIC_ICR_SEND_PENDING) {
    if (!--remaining) {
      panic("local APIC IPI delivery stalled");
    }
    __asm__ volatile("pause");
  }
  /* Writing the low half sends the command. Zero mode fields select a physical
   * destination, fixed delivery and edge triggering; no shorthand or broadcast. */
  apic_write(APIC_ICR_HIGH, destination << APIC_ICR_DESTINATION_SHIFT);
  apic_write(APIC_ICR_LOW, APIC_RESCHEDULE_VECTOR);
}

bool apic_try_send_tlb_flush(uint32_t destination)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(destination <= XAPIC_MAX_ID);
  if (apic_read(APIC_ICR_LOW) & APIC_ICR_SEND_PENDING) {
    return false;
  }
  apic_write(APIC_ICR_HIGH, destination << APIC_ICR_DESTINATION_SHIFT);
  apic_write(APIC_ICR_LOW, APIC_TLB_FLUSH_VECTOR);
  return true;
}

bool apic_try_send_nmi(uint32_t destination)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(destination <= XAPIC_MAX_ID);
  if (apic_read(APIC_ICR_LOW) & APIC_ICR_SEND_PENDING) {
    return false;
  }
  apic_write(APIC_ICR_HIGH, destination << APIC_ICR_DESTINATION_SHIFT);
  apic_write(APIC_ICR_LOW, APIC_ICR_DELIVERY_NMI);
  return true;
}
