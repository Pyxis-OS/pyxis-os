#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H
#include <stdarg.h>
/* Formatting subset is documented in <kernel/format.h>.
 * Normal calls are serialized per format invocation; callers keep IF=0.
 * Initialize/change the TTY on the BSP before releasing AP schedulers. */
void kvlog(const char *format, va_list args);
void klog(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* Protect the shared serial/TTY output. IF=0; no nesting. A false result
 * selects emergency serial output without acquiring the lock. Pair with
 * log_end(), and never access TTY state when acquisition returns false. */
bool log_begin(void);
void log_end(bool locked);
/* Low-level routing used inside a log_begin()/log_end() section. */
void log_putc(char c);
/* Irreversibly select unlocked serial-only output for fatal diagnostics.
 * Safe without GS, heap or a functioning lock owner; lines may interleave. */
void klog_panic_begin(void);
#endif
