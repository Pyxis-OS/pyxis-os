#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H
#include <stdarg.h>
#include <kernel-log-config.h>

struct tty;

/* Select an initialized log TTY on the BSP before releasing AP schedulers.
 * The first selection replays the retained early log under the log lock.
 * Saves/restores caller IF. Until the presenter's first frame, logging also
 * draws on the early console. */
void log_set_tty(struct tty *tty);

/* Formatting subset is documented in <kernel/format.h>.
 * Normal calls are serialized per format invocation and preserve caller IF.
 * Initialize/change the TTY on the BSP before releasing AP schedulers. */
void kvlog(const char *format, va_list args);
void klog(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Disabled traces still receive printf format checking, but their arguments
 * are not evaluated. Ordinary klog and fatal diagnostics are always enabled. */
#define ktrace(...) do { \
  if (KLOG_TRACE_ENABLED) { \
    klog(__VA_ARGS__); \
  } \
} while (0)

/* Protect the shared serial/TTY output. IF=0; no nesting. A false result
 * selects emergency serial output without acquiring the lock. Pair with
 * log_end(), and never access TTY state when acquisition returns false. */
bool log_begin(void);
void log_end(bool locked);
/* Low-level routing used inside a log_begin()/log_end() section. */
void log_putc(char c);
/* Irreversibly select unlocked serial-only output for fatal diagnostics.
 * Safe without GS, heap or a functioning lock owner; lines may interleave.
 * Ring capture tries its separate lock once, skipping bytes if it is held.
 * Before the presenter's handoff, the first panicking CPU also owns the
 * early framebuffer console. */
void klog_panic_begin(void);
#endif
