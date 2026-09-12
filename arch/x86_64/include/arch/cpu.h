#ifndef ARCH_CPU_H
#define ARCH_CPU_H
#include <stdint.h>
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
static inline void invlpg(uintptr_t address)
{
  __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
}
#endif
