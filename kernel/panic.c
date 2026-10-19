#include <arch/backtrace.h>
#include <arch/cpu.h>
#include <arch/debug.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/fb/early_console.h>

[[noreturn]] static void vpanic_at(uintptr_t rip, bool rip_returns, uintptr_t frame,
    const char *format, va_list args)
{
  cpu_disable_interrupts();
  if (arch_debug_enabled) {
    va_list local_args;
    va_copy(local_args, args);
    early_console_panic_vprintf(format, local_args);
    va_end(local_args);
    arch_debug_terminal(NULL, 0, DEBUG_STOP_PANIC);
  }
  klog_panic_begin();
  kerror("\nCaelum panic: ");
  kvlog(LOG_LEVEL_ERROR, format, args);
  kerror("\n");
  arch_fatal_report(rip, rip_returns, frame);
  cpu_halt();
}

[[noreturn]] void panic_at(uintptr_t rip, uintptr_t frame, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vpanic_at(rip, false, frame, format, args);
}

[[noreturn]] void panic(const char *format, ...)
{
  /* Frame pointers are kept, so our frame holds the caller's: a KASSERT in an
   * inlined helper reports the function it was inlined into. */
  uintptr_t caller_frame = *(const uintptr_t *)__builtin_frame_address(0);
  va_list args;
  va_start(args, format);
  vpanic_at((uintptr_t)__builtin_return_address(0), true, caller_frame, format, args);
}
