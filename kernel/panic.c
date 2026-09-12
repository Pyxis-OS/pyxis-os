#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/panic.h>

[[noreturn]] void panic(const char *format, ...)
{
  cpu_disable_interrupts();
  klog("\nCaelum panic: ");
  va_list args;
  va_start(args, format);
  kvlog(format, args);
  va_end(args);
  klog("\n");
  cpu_halt();
}
