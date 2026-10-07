#include <kernel/log.h>
#include <kernel/log_ring.h>
#include <arch/console.h>
#include <arch/cpu.h>
#include <stdatomic.h>
#include <kernel/fb/early_console.h>
#include <kernel/fb/tty.h>
#include <kernel/format.h>

static atomic_bool log_locked;
static atomic_bool panic_output;
static struct tty *log_tty;
/* Ordinary output and the one-time replay share the presentation lock. */
static bool early_log_replayed;

void log_set_tty(struct tty *tty)
{
  uint64_t flags = cpu_save_interrupts();
  bool locked = log_begin();
  if (locked) {
    if (!early_log_replayed) {
      struct log_snapshot snapshot = {0};
      if (log_ring_snapshot(&snapshot)) {
        struct log_cursor cursor = snapshot.first;
        struct log_read_reply reply;
        char text[LOG_READ_MAX];
        while (log_ring_read(cursor, snapshot.end, &reply, text, sizeof(text)) == CALL_OK &&
            reply.size) {
          for (size_t i = 0; i < reply.size; ++i) {
            tty_put_char(tty, text[i]);
          }
          cursor = reply.next;
        }
      }
      if (snapshot.dropped_lines) {
        char notice[96];
        sprintf(notice, "\n[early log truncated: %lu lines dropped]\n", snapshot.dropped_lines);
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
  log_ring_panic_begin();
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
  log_ring_putc(c);
  console_putc(c);
  if (atomic_load_explicit(&panic_output, memory_order_relaxed)) {
    early_console_panic_putc(c);
    return;
  }
  early_console_putc(c);
  if (log_tty && log_tty->initialized)
    tty_put_char(log_tty, c);
}
