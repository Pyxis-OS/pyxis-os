#include <kernel/log.h>
#include <arch/console.h>
#include <arch/cpu.h>
#include <stdatomic.h>
#include <kernel/fb/tty.h>

static atomic_bool log_locked;
static atomic_bool panic_output;
static struct tty *log_tty;

void log_set_tty(struct tty *tty)
{
  log_tty = tty;
}

void klog_panic_begin(void)
{
  /* A fatal exception may interrupt the lock owner before GS is usable. */
  atomic_store_explicit(&panic_output, true, memory_order_relaxed);
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
  if (!atomic_load_explicit(&panic_output, memory_order_relaxed) &&
      log_tty && log_tty->initialized)
    tty_put_char(log_tty, c);
}
