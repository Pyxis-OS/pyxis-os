#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stddef.h>
#include <stdint.h>
#include <arch/syscall.h>

#define CPUID_BASIC_FEATURES 1
#define CPUID_FEATURE_PAT (1u << 16)
#define CPUID_EXTENDED_FEATURES 0x80000001
#define CPUID_FEATURE_SYSCALL (1u << 11)
#define CPUID_FEATURE_NX (1u << 20)

#define IA32_EFER 0xc0000080
#define IA32_STAR 0xc0000081
#define IA32_LSTAR 0xc0000082
#define IA32_FMASK 0xc0000084
#define IA32_PAT 0x277
#define IA32_FS_BASE 0xc0000100
#define IA32_GS_BASE 0xc0000101
#define IA32_KERNEL_GS_BASE 0xc0000102
#define EFER_SCE (UINT64_C(1) << 0)
#define EFER_NXE (UINT64_C(1) << 11)

/* Nominal preemption frequency, retained with deadline-driven timer arms. */
uint32_t arch_timer_frequency(void);

/* Local CPU, IF=0, after scheduler startup. Boot keeps its periodic timer
 * until this transition; in particular, AP startup polls the BSP countdown. */
void arch_timer_deadline_start(void);
/* Local CPU, IF=0. Whether arming this absolute deadline would target an
 * earlier time than the pending countdown, or none is pending. A later target
 * keeps the earlier interrupt, which rearms when it finds nothing due. Always
 * false before the transition. */
bool arch_timer_arm_needed(uint64_t deadline);
/* Local CPU, IF=0. Arm the earlier of this absolute deadline and the next
 * nominal preemption occasion. UINT64_MAX means no task deadline. now is a
 * monotonic reading the caller took on this CPU; the interrupt arrives that
 * much later than the target for however old the reading is. Before the
 * transition this leaves the bootstrap periodic timer unchanged. */
void arch_timer_arm(uint64_t deadline, uint64_t now);
/* Local timer entry, IF=0. Read the clock once, advance the nominal preemption
 * phase when due and service BSP HPET maintenance; return the reading for the
 * scheduler's expiry and rearm, which must follow before interrupt
 * acknowledgement or any context switch. */
uint64_t arch_timer_interrupt(void);

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
  __asm__ volatile("cpuid"
                   : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                   : "a"(leaf), "c"(0));
}

#define CPUID_VENDOR 0
#define CPUID_X2APIC_TOPOLOGY 0xb
#define CPUID_INITIAL_APIC_ID_SHIFT 24

/* This CPU's APIC ID from CPUID; needs no GS, LAPIC mapping or memory. Uses
 * the 32-bit x2APIC ID when leaf 0xb exists, otherwise the 8-bit initial ID. */
static inline uint32_t cpu_initial_apic_id(void)
{
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_VENDOR, &eax, &ebx, &ecx, &edx);
  if (eax >= CPUID_X2APIC_TOPOLOGY) {
    cpuid(CPUID_X2APIC_TOPOLOGY, &eax, &ebx, &ecx, &edx);
    if (ebx) {
      return edx;
    }
  }
  cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
  return ebx >> CPUID_INITIAL_APIC_ID_SHIFT;
}

static inline void cpu_disable_interrupts(void)
{
  __asm__ volatile("cli" : : : "memory");
}

static inline void cpu_enable_interrupts(void)
{
  __asm__ volatile("sti" : : : "memory");
}

static inline uint64_t cpu_save_interrupts(void)
{
  uint64_t flags;
  __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
  return flags;
}

static inline void cpu_restore_interrupts(uint64_t flags)
{
  if (flags & RFLAGS_INTERRUPT_ENABLE) {
    cpu_enable_interrupts();
  }
}

/* Drain write-combining stores before reporting framebuffer writes complete. */
static inline void cpu_store_fence(void)
{
  __asm__ volatile("sfence" : : : "memory");
}

/* Forward copy with string moves: eight-byte words, then the remaining bytes.
 * Every kernel entry clears DF (isr.S and the syscall FMASK), so the moves run
 * forward. Overlapping ranges are not supported. */
static inline void cpu_copy_forward(void *dest, const void *src, size_t count)
{
  size_t words = count / sizeof(uint64_t);
  size_t bytes = count % sizeof(uint64_t);
  __asm__ volatile("rep movsq" : "+D"(dest), "+S"(src), "+c"(words) : : "memory");
  __asm__ volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(bytes) : : "memory");
}

static inline void outb(uint16_t port, uint8_t value)
{
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
  __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
  uint16_t value;
  __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static inline void outl(uint16_t port, uint32_t value)
{
  __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
  uint32_t value;
  __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

[[noreturn]] static inline void cpu_halt(void)
{
  for (;;) {
    __asm__ volatile("cli; hlt" : : : "memory");
  }
}

/* STI's interrupt shadow makes enabling interrupts and sleeping atomic.
 * Return to the scheduler with IF=0 after the interrupt handler resumes us. */
static inline void cpu_wait_interrupt(void)
{
  __asm__ volatile("sti; hlt; cli" : : : "memory");
}

static inline uint64_t read_cr0(void)
{
  uint64_t value;
  __asm__ volatile("mov %%cr0, %0" : "=r"(value));
  return value;
}

static inline void write_cr0(uint64_t value)
{
  __asm__ volatile("mov %0, %%cr0" : : "r"(value) : "memory");
}

static inline uint64_t read_cr2(void)
{
  uint64_t value;
  __asm__ volatile("mov %%cr2, %0" : "=r"(value));
  return value;
}

static inline uint64_t read_cr3(void)
{
  uint64_t value;
  __asm__ volatile("mov %%cr3, %0" : "=r"(value));
  return value;
}

static inline void write_cr3(uint64_t value)
{
  __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static inline uint64_t read_cr4(void)
{
  uint64_t value;
  __asm__ volatile("mov %%cr4, %0" : "=r"(value));
  return value;
}

static inline void write_cr4(uint64_t value)
{
  __asm__ volatile("mov %0, %%cr4" : : "r"(value) : "memory");
}

static inline uint64_t read_msr(uint32_t number)
{
  uint32_t low, high;
  __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(number));
  return ((uint64_t)high << 32) | low;
}

static inline void write_msr(uint32_t number, uint64_t value)
{
  __asm__ volatile("wrmsr" : : "c"(number), "a"((uint32_t)value),
                   "d"((uint32_t)(value >> 32)) : "memory");
}

static inline void invlpg(uintptr_t address)
{
  __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
}

#endif
