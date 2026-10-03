#include <kernel/log.h>
#include <arch/console.h>
#include <arch/cpu.h>
#include <stdatomic.h>
#include <kernel/fb/early_console.h>
#include <kernel/fb/tty.h>
#include <kernel/format.h>

#define EARLY_LOG_BYTES (32 * 1024)

static atomic_bool log_locked;
static atomic_bool panic_output;
static struct tty *log_tty;
/* Ordinary output and the one-time replay share the log lock. */
static char early_log[EARLY_LOG_BYTES];
static size_t early_log_length;
static size_t early_log_dropped;
static bool early_log_replayed;

void log_set_tty(struct tty *tty)
{
  uint64_t flags = cpu_save_interrupts();
  bool locked = log_begin();
  if (locked) {
    if (!early_log_replayed) {
      for (size_t i = 0; i < early_log_length; ++i) {
        tty_put_char(tty, early_log[i]);
      }
      if (early_log_dropped) {
        char notice[96];
        sprintf(notice, "\n[early log truncated: %zu bytes dropped]\n", early_log_dropped);
        for (const char *c = notice; *c; ++c) {
          tty_put_char(tty, *c);
        }
      }
      early_log_replayed = true;
    }
    log_tty = tty;
  }
  log_end(locked);
  cpu_restore_interrupts(flags);
}

void klog_panic_begin(void)
{
  /* A fatal exception may interrupt the lock owner before GS is usable. */
  atomic_store_explicit(&panic_output, true, memory_order_relaxed);
  early_console_panic_begin();
}

bool log_begin(void)
{
  while (!atomic_load_explicit(&panic_output, memory_order_relaxed)) {
    if (!atomic_exchange_explicit(&log_locked, true, memory_order_acquire)) {
      return true;
    }
    __asm__ volatile("pause");
  }
  return false;
}

void log_end(bool locked)
{
  if (locked) {
    /* Drain write-combining framebuffer stores before handing off the TTY. */
    cpu_store_fence();
    atomic_store_explicit(&log_locked, false, memory_order_release);
  }
}

void klog(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  kvlog(format, args);
  va_end(args);
}

void log_putc(char c)
{
  console_putc(c);
  if (atomic_load_explicit(&panic_output, memory_order_relaxed)) {
    early_console_panic_putc(c);
    return;
  }
  /* Ordinary output holds the log lock here. */
  if (!early_log_replayed) {
    if (early_log_length < sizeof(early_log)) {
      early_log[early_log_length++] = c;
    } else if (early_log_dropped < SIZE_MAX) {
      ++early_log_dropped;
    }
  }
  early_console_putc(c);
  if (log_tty && log_tty->initialized)
    tty_put_char(log_tty, c);
}
