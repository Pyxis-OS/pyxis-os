#include <arch/cpu.h>
#include <arch/debug.h>
#include <kernel/log.h>
#include <kernel/panic.h>

[[noreturn]] void panic(const char *format, ...)
{
  cpu_disable_interrupts();
  if (arch_debug_enabled) {
    arch_debug_terminal(NULL, 0, DEBUG_STOP_PANIC);
  }
  klog_panic_begin();
  klog("\nCaelum panic: ");
  va_list args;
  va_start(args, format);
  kvlog(format, args);
  va_end(args);
  klog("\n");
  cpu_halt();
}
