#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H
#include <abi/log.h>
#include <stddef.h>
#include <stdarg.h>
#include <kernel-log-config.h>

struct tty;

/* Select an initialized log TTY on the BSP before releasing AP schedulers.
 * The first selection replays the retained early log under the log lock.
 * Saves/restores caller IF. Until the presenter's first frame, logging also
 * draws on the early console. */
void log_set_tty(struct tty *tty);

/* Set once on the BSP from boot options, before releasing AP schedulers.
 * Records at or above this level reach serial, the early console and the
 * log TTY; the ring keeps every compiled-in record. Default: info. */
void log_set_console_level(enum log_level level);

/* Formatting subset is documented in <kernel/format.h>.
 * Normal calls are serialized per format invocation and preserve caller IF.
 * A line's level is the level of the record that started it. Records deeper
 * than LOG_LEVEL_CEILING are dropped. Call sites use the macros below; klog
 * is for callers that hold a level at run time. */
void kvlog(enum log_level level, const char *format, va_list args);
void klog(enum log_level level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#define kerror(...) klog(LOG_LEVEL_ERROR, __VA_ARGS__)
#define kwarn(...) klog(LOG_LEVEL_WARN, __VA_ARGS__)
#define kinfo(...) klog(LOG_LEVEL_INFO, __VA_ARGS__)

/* Beyond the build ceiling, records still receive printf format checking,
 * but their arguments are not evaluated. */
#define kdebug(...) do { \
  if (LOG_LEVEL_CEILING >= LOG_LEVEL_DEBUG) { \
    klog(LOG_LEVEL_DEBUG, __VA_ARGS__); \
  } \
} while (0)

#define ktrace(...) do { \
  if (LOG_LEVEL_CEILING >= LOG_LEVEL_TRACE) { \
    klog(LOG_LEVEL_TRACE, __VA_ARGS__); \
  } \
} while (0)

/* Protect the shared serial/TTY output. IF=0; no nesting. A false result
 * selects emergency serial output without acquiring the lock. Pair with
 * log_end(), and never access TTY state when acquisition returns false. */
bool log_begin(void);
void log_end(bool locked);
/* Low-level routing used inside a log_begin()/log_end() section. Output with
 * no level of its own is info. log_record_begin/end bracket one record's
 * characters so the log TTY can colour it. */
void log_putc(enum log_level level, char c);
void log_record_begin(enum log_level level);
void log_record_end(enum log_level level);
/* Logs one line written by SPACE at LEVEL under its index and name, as
 * "NAME: TEXT\n". TEXT is already sanitised and at most LOG_WRITE_MAX bytes.
 * Charges the space's rate bucket; a line beyond it is dropped and counted, and
 * the next logged line reports the count at warn. Any CPU, preserves IF. */
struct space;
void log_write_line(struct space *space, enum log_level level, const char *text, size_t length);
/* Irreversibly select unlocked emergency output for fatal diagnostics.
 * Safe without GS, heap or a functioning lock owner; lines may interleave.
 * Ring capture tries its separate lock once, skipping bytes if it is held.
 * The first panicking CPU may also claim the direct framebuffer console,
 * before or after the presenter's handoff. */
void klog_panic_begin(void);
/* True once fatal output has started; ordinary output locks stop waiting. */
bool log_panic_started(void);
#endif
