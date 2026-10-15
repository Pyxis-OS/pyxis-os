#ifndef ABI_LOG_H
#define ABI_LOG_H

#include <abi/message.h>

#define LOG_RIGHT_READ (UINT64_C(1) << 0)
#define LOG_SNAPSHOT UINT64_C(1)
#define LOG_READ UINT64_C(2)
#define LOG_READ_MAX UINT64_C(1024)

/* Ordered from most to least severe. Every retained line has one level. */
enum log_level {
  LOG_LEVEL_ERROR = 1,
  LOG_LEVEL_WARN,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_TRACE,
};

/* Boot-local text position. A line is delimited by '\n', independently of
 * formatting calls. Offset permits bounded reads of an unfinished/long line.
 * Start at {0, 0}, then reuse returned cursors. No per-reader kernel state. */
struct log_cursor {
  uint64_t line;
  uint64_t offset;
};

/* SNAPSHOT is header-only. The fixed ring includes five bytes per stored line.
 * first/end delimit retained text at this observation; end may be mid-line.
 * dropped_lines counts whole lines evicted since boot, including an oversized
 * line discarded when it cannot fit. No persistence, clearing or write right. */
struct log_snapshot {
  struct log_cursor first;
  struct log_cursor end;
  uint64_t dropped_lines;
  uint64_t capacity_bytes;
};

/* READ is nonblocking. end is a previously captured snapshot end, or
 * {UINT64_MAX, UINT64_MAX} to read through the current end. Cursor/end are
 * ordered lexicographically. A lagging cursor skips to the oldest retained
 * line (at most end), reporting the number of skipped lines. An empty reply
 * means caught up to the selected end, not permanent EOF. Readers consume
 * nothing globally; explicit grants alone confer authority to read.
 * Reply is log_read_reply followed by size bytes of packed records, at most
 * LOG_READ_MAX. Capacity must include the header, one record header and one
 * text byte. Future positions/invalid retained offsets are BAD_REQUEST. Once
 * overwritten, old byte offsets cannot be validated. Fatal-path capture is
 * best effort and never waits for an interrupted owner. */
struct log_read_request {
  struct message_header header;
  struct log_cursor cursor;
  struct log_cursor end;
};

struct log_read_reply {
  struct log_cursor next;
  uint64_t dropped_lines;
  uint64_t size;
};

/* One piece of one line, followed by length (at least one) raw text bytes
 * without a NUL. Only the final text byte of a record may be '\n'; a record
 * without one ends the reply, and the line continues in the next READ at the
 * same level. Records are packed, so headers may be unaligned. reserved is 0. */
struct log_record {
  uint16_t length;
  uint8_t level;
  uint8_t reserved;
};

_Static_assert(sizeof(struct log_cursor) == 16, "log cursor layout");
_Static_assert(sizeof(struct log_snapshot) == 48, "log snapshot layout");
_Static_assert(sizeof(struct log_read_request) == 48, "log read request layout");
_Static_assert(sizeof(struct log_read_reply) == 32, "log read reply layout");
_Static_assert(sizeof(struct log_record) == 4, "log record layout");

#endif
