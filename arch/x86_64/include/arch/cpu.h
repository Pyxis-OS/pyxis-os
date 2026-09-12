#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stdint.h>

#define CPUID_EXTENDED_FEATURES 0x80000001
#define CPUID_FEATURE_SYSCALL (1u << 11)
#define CPUID_FEATURE_NX (1u << 20)

#define IA32_EFER 0xc0000080
#define IA32_STAR 0xc0000081
#define EFER_SCE (UINT64_C(1) << 0)
#define EFER_NXE (UINT64_C(1) << 11)

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
  __asm__ volatile("cpuid"
                   : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                   : "a"(leaf), "c"(0));
}

static inline void cpu_disable_interrupts(void)
{
  __asm__ volatile("cli" : : : "memory");
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

[[noreturn]] static inline void cpu_halt(void)
{
  for (;;) {
    __asm__ volatile("cli; hlt" : : : "memory");
  }
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
