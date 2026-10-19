#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H
#include <stdint.h>
/* Both log the message, then the CPU, RIP and a frame-pointer backtrace. */
[[noreturn]] void panic(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
/* RIP is the failing instruction itself, as for an exception, and FRAME is
 * the frame pointer of the function containing it. */
[[noreturn]] void panic_at(uintptr_t rip, uintptr_t frame, const char *format, ...)
  __attribute__((format(printf, 3, 4)));
#define KASSERT(condition) \
  ((condition) ? (void)0 : panic("assertion: %s (%s:%d)", #condition, __FILE__, __LINE__))
#endif
