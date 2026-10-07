#include <arch/cpu.h>
#include <kernel/log_ring.h>
#include <stdatomic.h>

#define LOG_RING_BYTES (256 * 1024)
#define LOG_LINE_HEADER_BYTES sizeof(uint32_t)

static char ring[LOG_RING_BYTES];
static atomic_bool ring_locked;
static atomic_bool ring_panic;
static size_t first_index, used_bytes, current_index;
static uint32_t current_length;
static uint64_t first_line, next_line;
static bool line_open, discard_line, exhausted;

/* Fatal entry may interrupt a writer or reader on this CPU, including before
 * GS is usable. Once panic starts, every acquisition becomes a single try. */
static bool lock_ring(void)
{
  for (;;) {
    if (!atomic_exchange_explicit(&ring_locked, true, memory_order_acquire)) {
      return true;
    }
    if (atomic_load_explicit(&ring_panic, memory_order_relaxed)) {
      return false;
    }
    __asm__ volatile("pause");
  }
}

static void unlock_ring(void)
{
  atomic_store_explicit(&ring_locked, false, memory_order_release);
}

void log_ring_panic_begin(void)
{
  atomic_store_explicit(&ring_panic, true, memory_order_relaxed);
}

static size_t ring_index(size_t index)
{
  return index % sizeof(ring);
}

/* Headers may wrap or be unaligned, so decode them as bytes. */
static uint32_t line_length(size_t index)
{
  uint32_t length = 0;
  for (size_t i = 0; i < LOG_LINE_HEADER_BYTES; ++i) {
    length |= (uint32_t)(unsigned char)ring[ring_index(index + i)] << (i * 8);
  }
  return length;
}

static void set_line_length(size_t index, uint32_t length)
{
  for (size_t i = 0; i < LOG_LINE_HEADER_BYTES; ++i) {
    ring[ring_index(index + i)] = (char)(length >> (i * 8));
  }
}

static void drop_first_line(void)
{
  size_t bytes = LOG_LINE_HEADER_BYTES + line_length(first_index);
  if (line_open && first_line == next_line) {
    /* This one line fills the entire ring. Discard its remaining characters
     * through the newline, rather than exposing an unmarked suffix. */
    line_open = false;
    discard_line = true;
    ++next_line;
  }
  first_index = ring_index(first_index + bytes);
  used_bytes -= bytes;
  ++first_line;
}

void log_ring_putc(char c)
{
  if (!lock_ring()) {
    return;
  }
  /* Sequence wrap must never make old reader cursors alias new lines. */
  if (next_line == UINT64_MAX) {
    exhausted = true;
  }
  if (exhausted) {
    unlock_ring();
    return;
  }
  if (!discard_line) {
    if (!line_open) {
      while (sizeof(ring) - used_bytes < LOG_LINE_HEADER_BYTES + 1) {
        drop_first_line();
      }
      current_index = ring_index(first_index + used_bytes);
      current_length = 0;
      set_line_length(current_index, 0);
      used_bytes += LOG_LINE_HEADER_BYTES;
      line_open = true;
    }
    if (used_bytes == sizeof(ring)) {
      drop_first_line();
    }
    if (!discard_line) {
      ring[ring_index(first_index + used_bytes)] = c;
      ++used_bytes;
      set_line_length(current_index, ++current_length);
      if (c == '\n') {
        line_open = false;
        ++next_line;
      }
    }
  }
  if (discard_line && c == '\n') {
    discard_line = false;
  }
  unlock_ring();
}

static struct log_cursor ring_end(void)
{
  return (struct log_cursor){next_line, line_open ? current_length : 0};
}

static int compare_cursor(struct log_cursor a, struct log_cursor b)
{
  if (a.line != b.line) {
    return a.line < b.line ? -1 : 1;
  }
  return a.offset < b.offset ? -1 : a.offset > b.offset;
}

bool log_ring_snapshot(struct log_snapshot *snapshot)
{
  uint64_t flags = cpu_save_interrupts();
  bool locked = lock_ring();
  if (locked) {
    *snapshot = (struct log_snapshot){
      .first = {first_line, 0},
      .end = ring_end(),
      .dropped_lines = first_line,
      .capacity_bytes = sizeof(ring),
    };
    unlock_ring();
  }
  cpu_restore_interrupts(flags);
  return locked;
}

static bool find_line(struct log_cursor cursor, size_t *index)
{
  *index = first_index;
  for (uint64_t line = first_line; line < cursor.line; ++line) {
    *index = ring_index(*index + LOG_LINE_HEADER_BYTES + line_length(*index));
  }
  if (cursor.line == next_line && !line_open) {
    return cursor.offset == 0;
  }
  uint32_t length = line_length(*index);
  return cursor.line < next_line ? cursor.offset < length : cursor.offset <= length;
}

static enum call_status read_locked(struct log_cursor cursor, struct log_cursor end,
    struct log_read_reply *reply, char *text, size_t capacity)
{
  struct log_cursor tail = ring_end();
  if (end.line == UINT64_MAX && end.offset == UINT64_MAX) {
    end = tail;
  }
  if (compare_cursor(end, tail) > 0 || compare_cursor(cursor, end) > 0) {
    return CALL_BAD_REQUEST;
  }
  *reply = (struct log_read_reply){.next = cursor};
  if (compare_cursor(cursor, end) == 0) {
    size_t index;
    if (cursor.line >= first_line && !find_line(cursor, &index)) {
      return CALL_BAD_REQUEST;
    }
    return CALL_OK;
  }
  struct log_cursor first = {first_line, 0};
  if (cursor.line < first_line) {
    if (compare_cursor(first, end) > 0) {
      reply->dropped_lines = end.line - cursor.line + (end.offset != 0);
      reply->next = end;
      return CALL_OK;
    }
    reply->dropped_lines = first_line - cursor.line;
    cursor = first;
  }
  size_t index, end_index;
  if (!find_line(cursor, &index) || !find_line(end, &end_index)) {
    return CALL_BAD_REQUEST;
  }
  while (compare_cursor(cursor, end) < 0 && reply->size < capacity) {
    uint32_t length = line_length(index);
    size_t available = length - cursor.offset;
    if (cursor.line == end.line && available > end.offset - cursor.offset) {
      available = end.offset - cursor.offset;
    }
    if (available > capacity - reply->size) {
      available = capacity - reply->size;
    }
    for (size_t i = 0; i < available; ++i) {
      text[reply->size++] = ring[ring_index(index + LOG_LINE_HEADER_BYTES + cursor.offset++)];
    }
    if (cursor.offset == length && cursor.line < next_line &&
        compare_cursor(cursor, end) < 0) {
      index = ring_index(index + LOG_LINE_HEADER_BYTES + length);
      ++cursor.line;
      cursor.offset = 0;
    }
  }
  reply->next = cursor;
  return CALL_OK;
}

enum call_status log_ring_read(struct log_cursor cursor, struct log_cursor end,
    struct log_read_reply *reply, char *text, size_t capacity)
{
  uint64_t flags = cpu_save_interrupts();
  if (!lock_ring()) {
    cpu_restore_interrupts(flags);
    return CALL_UNAVAILABLE;
  }
  enum call_status status = read_locked(cursor, end, reply, text, capacity);
  unlock_ring();
  cpu_restore_interrupts(flags);
  return status;
}
