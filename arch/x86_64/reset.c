#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/descriptors.h>
#include <arch/reset.h>

#define PS2_COMMAND_PORT 0x64
#define PS2_STATUS_PORT 0x64
#define PS2_INPUT_FULL (1u << 1)
#define PS2_PULSE_RESET_LINE 0xfe
#define INPUT_WAIT_NS UINT64_C(10000000)
#define RESET_WAIT_NS UINT64_C(500000000)

static void wait_ns(uint64_t nanoseconds)
{
  uint64_t end = arch_monotonic_ns() + nanoseconds;
  while (arch_monotonic_ns() < end) {
    __asm__ volatile("pause");
  }
}

[[noreturn]] void arch_reset_fallback(void)
{
  cpu_disable_interrupts();
  uint64_t end = arch_monotonic_ns() + INPUT_WAIT_NS;
  while ((inb(PS2_STATUS_PORT) & PS2_INPUT_FULL) && arch_monotonic_ns() < end) {
    __asm__ volatile("pause");
  }
  outb(PS2_COMMAND_PORT, PS2_PULSE_RESET_LINE);
  wait_ns(RESET_WAIT_NS);

  /* With an empty IDT the breakpoint cannot be delivered, nor can the double
   * fault that follows, so the CPU shuts down and the platform resets. */
  const struct descriptor_table_pointer empty = {0};
  __asm__ volatile("lidt %0; int3" : : "m"(empty) : "memory");
  cpu_halt();
}
