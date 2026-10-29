#include <abi/log.h>
#include <arch/clock.h>
#include <kernel/log.h>
#include <kernel/log_ring.h>
#include <kernel/space.h>
#include <kernel/net/log_udp.h>
#include <arch/console.h>
#include <arch/cpu.h>
#include <stdatomic.h>
#include <kernel/fb/early_console.h>
#include <kernel/fb/tty.h>
#include <kernel/format.h>
#include <kernel/memory.h>

/* SGR foreground selections from the log TTY's 16-colour palette. */
#define LOG_COLOUR_ERROR "\x1b[91m"
#define LOG_COLOUR_WARN "\x1b[93m"
#define LOG_COLOUR_DETAIL "\x1b[37m"
#define LOG_COLOUR_DEFAULT "\x1b[39m"

static atomic_bool log_locked;
/* A line written by a space is being logged; the log lock protects it. */
static bool log_user_line;
static atomic_bool panic_output;
static struct tty *log_tty;
/* Ordinary output and the one-time replay share the presentation lock. */
static bool early_log_replayed;
static enum log_level console_level = LOG_LEVEL_INFO;

void log_set_console_level(enum log_level level)
{
  console_level = level;
}

static bool console_shows(enum log_level level)
{
  return level <= console_level;
}

/* Info keeps the TTY's default colour. */
static const char *level_colour(enum log_level level)
{
  switch (level) {
  case LOG_LEVEL_ERROR:
    return LOG_COLOUR_ERROR;
  case LOG_LEVEL_WARN:
    return LOG_COLOUR_WARN;
  case LOG_LEVEL_DEBUG:
  case LOG_LEVEL_TRACE:
    return LOG_COLOUR_DETAIL;
  default:
    return NULL;
  }
}

static void tty_put_text(struct tty *tty, const char *text)
{
  for (; *text; ++text) {
    tty_put_char(tty, *text);
  }
}

/* Caller holds the TTY lock. Hidden levels are skipped, as for live output. */
static void replay_records(struct tty *tty, const char *bytes, size_t size)
{
  size_t offset = 0;
  while (offset < size) {
    struct log_record record;
    memcpy(&record, bytes + offset, sizeof(record));
    const char *text = bytes + offset + sizeof(record);
    offset += sizeof(record) + record.length;
    if (!console_shows(record.level)) {
      continue;
    }
    const char *colour = level_colour(record.level);
    if (colour) {
      tty_put_text(tty, colour);
    }
    for (size_t i = 0; i < record.length; ++i) {
      tty_put_char(tty, text[i]);
    }
    if (colour) {
      tty_put_text(tty, LOG_COLOUR_DEFAULT);
    }
  }
}

void log_set_tty(struct tty *tty)
{
  uint64_t flags = cpu_save_interrupts();
  bool locked = log_begin();
  if (locked && tty_lock(tty)) {
    if (!early_log_replayed) {
      struct log_snapshot snapshot = {0};
      if (log_ring_snapshot(&snapshot)) {
        struct log_cursor cursor = snapshot.first;
        struct log_read_reply reply;
        char bytes[LOG_READ_MAX];
        while (log_ring_read(cursor, snapshot.end, &reply, bytes, sizeof(bytes)) == CALL_OK &&
            reply.size) {
          replay_records(tty, bytes, reply.size);
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
    tty_unlock(tty);
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
  net_log_udp_panic_begin();
  early_console_panic_begin();
}

bool log_panic_started(void)
{
  return atomic_load_explicit(&panic_output, memory_order_relaxed);
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

void klog(enum log_level level, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  kvlog(level, format, args);
  va_end(args);
}

static void log_tty_put_text(const char *text)
{
  if (log_tty && log_tty->initialized && tty_lock(log_tty)) {
    tty_put_text(log_tty, text);
    tty_unlock(log_tty);
  }
}

void log_record_begin(enum log_level level)
{
  const char *colour = level_colour(level);
  if (colour && console_shows(level) &&
      !atomic_load_explicit(&panic_output, memory_order_relaxed)) {
    log_tty_put_text(colour);
  }
}

/* Later output without a level, such as boot init's console, starts plain. */
void log_record_end(enum log_level level)
{
  if (level_colour(level) && console_shows(level) &&
      !atomic_load_explicit(&panic_output, memory_order_relaxed)) {
    log_tty_put_text(LOG_COLOUR_DEFAULT);
  }
}

void log_putc(enum log_level level, char c)
{
  net_log_udp_panic_putc(level, c);
  log_ring_putc(level, c);
  if (!console_shows(level)) {
    return;
  }
  bool panicking = atomic_load_explicit(&panic_output, memory_order_relaxed);
  /* Serial is slow (a few milliseconds per line) and the log lock is held
   * throughout, so lines written by spaces stay off it and off the early console.
   * They remain in the ring, on the Caelum TTY and in UDP capture. */
  bool mirrored = !log_user_line || panicking;
  if (mirrored) {
    console_putc(c);
  }
  if (panicking) {
    early_console_panic_putc(c);
    return;
  }
  if (mirrored) {
    early_console_putc(c);
  }
  /* Log lock, then the TTY's output lock: the documented order. */
  if (log_tty && log_tty->initialized && tty_lock(log_tty)) {
    tty_put_char(log_tty, c);
    tty_unlock(log_tty);
  }
}

static void log_emit_line(uint8_t source, enum log_level level, const char *name,
    const char *text, size_t length)
{
  log_ring_set_source(source);
  log_user_line = source != 0;
  log_record_begin(level);
  for (const char *c = name; *c; ++c) {
    log_putc(level, *c);
  }
  log_putc(level, ':');
  log_putc(level, ' ');
  for (size_t i = 0; i < length; ++i) {
    log_putc(level, text[i]);
  }
  log_putc(level, '\n');
  log_record_end(level);
  log_user_line = false;
  log_ring_set_source(0);
}

/* Refill one line per LOG_RATE_LINES_PER_SECOND's interval, up to the burst. */
static bool log_space_may_write(struct space *space, uint64_t now)
{
  const uint64_t interval = UINT64_C(1000000000) / LOG_RATE_LINES_PER_SECOND;
  uint64_t lines = (now - space->log_refilled_ns) / interval;
  if (lines) {
    space->log_tokens = lines >= LOG_RATE_BURST - space->log_tokens ?
        LOG_RATE_BURST : space->log_tokens + lines;
    space->log_refilled_ns = space->log_tokens == LOG_RATE_BURST ? now :
        space->log_refilled_ns + lines * interval;
  }
  if (!space->log_tokens) {
    return false;
  }
  --space->log_tokens;
  return true;
}

/* Caller holds the log lock. */
static void log_write_line_locked(struct space *space, enum log_level level, const char *text,
    size_t length)
{
  uint64_t now = arch_monotonic_ns();
  if (!log_space_may_write(space, now)) {
    ++space->log_dropped;
    return;
  }
  if (space->log_dropped && now - space->log_summary_ns >= UINT64_C(1000000000)) {
    char count[24];
    size_t digits = 0;
    for (uint64_t value = space->log_dropped; value; value /= 10) {
      count[digits++] = (char)('0' + value % 10);
    }
    char summary[64];
    size_t used = 0;
    static const char prefix[] = "dropped ";
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i) {
      summary[used++] = prefix[i];
    }
    for (const char *c = space->name; *c && used < 40; ++c) {
      summary[used++] = *c;
    }
    summary[used++] = ' ';
    while (digits) {
      summary[used++] = count[--digits];
    }
    static const char suffix[] = " log lines";
    for (size_t i = 0; i < sizeof(suffix) - 1; ++i) {
      summary[used++] = suffix[i];
    }
    log_emit_line(0, LOG_LEVEL_WARN, "log", summary, used);
    space->log_dropped = 0;
    space->log_summary_ns = now;
  }
  log_emit_line(space->log_source, level, space->name, text, length);
}

void log_write_line(struct space *space, enum log_level level, const char *text, size_t length)
{
  uint64_t flags = cpu_save_interrupts();
  bool locked = log_begin();
  if (locked) {
    log_write_line_locked(space, level, text, length);
  }
  log_end(locked);
  cpu_restore_interrupts(flags);
}

void log_putchar(struct space *space, char c)
{
  uint64_t flags = cpu_save_interrupts();
  bool locked = log_begin();
  if (!locked) {
    /* Fatal output has started: keep the old unlocked, unattributed path. */
    log_putc(LOG_LEVEL_INFO, c);
  } else if (c == '\n' || space->log_line_length == sizeof(space->log_line)) {
    log_write_line_locked(space, LOG_LEVEL_INFO, space->log_line, space->log_line_length);
    space->log_line_length = 0;
    if (c != '\n' && c != '\r') {
      space->log_line[space->log_line_length++] =
          (unsigned char)c < ' ' && c != '\t' ? '?' : c;
    }
  } else if (c != '\r') {
    space->log_line[space->log_line_length++] =
        ((unsigned char)c < ' ' && c != '\t') || c == 0x7f ? '?' : c;
  }
  log_end(locked);
  cpu_restore_interrupts(flags);
}
