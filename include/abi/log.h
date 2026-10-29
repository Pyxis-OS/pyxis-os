#ifndef ABI_LOG_H
#define ABI_LOG_H

#include <abi/handle.h>
#include <abi/message.h>

#define LOG_RIGHT_READ (UINT64_C(1) << 0)
#define LOG_RIGHT_WRITE (UINT64_C(1) << 1)
#define LOG_SNAPSHOT UINT64_C(1)
#define LOG_READ UINT64_C(2)
#define LOG_WRITE UINT64_C(3)
#define LOG_STREAM UINT64_C(4)
#define LOG_READ_MAX UINT64_C(1024)
/* Longest text of one written line; a longer write is cut at this length. */
#define LOG_WRITE_MAX UINT64_C(255)
/* A space may write this many lines per second, with this burst. Lines beyond
 * it are dropped and the kernel logs one warning per second counting them. */
#define LOG_RATE_LINES_PER_SECOND UINT64_C(1000)
#define LOG_RATE_BURST UINT64_C(5000)

/* Ordered from most to least severe. Every retained line has one level. */
enum log_level {
  LOG_LEVEL_ERROR = 1,
  LOG_LEVEL_WARN = 2,
  LOG_LEVEL_INFO = 3,
  LOG_LEVEL_DEBUG = 4,
  LOG_LEVEL_TRACE = 5,
};

/* Boot-local text position. A line is delimited by '\n', independently of
 * formatting calls. Offset permits bounded reads of an unfinished/long line.
 * Start at {0, 0}, then reuse returned cursors. No per-reader kernel state. */
struct log_cursor {
  uint64_t line;
  uint64_t offset;
};

/* SNAPSHOT is header-only. The fixed ring includes six bytes per stored line.
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
 * same level and source. Records are packed, so headers may be unaligned.
 * source is 0 for the kernel and the stamped index of the writing space
 * otherwise; the text of a written line starts with that space's name and ": ". */
struct log_record {
  uint16_t length;
  uint8_t level;
  uint8_t source;
};

/* LOG_WRITE needs LOG_RIGHT_WRITE. One call is one line: level (enum log_level)
 * and up to LOG_WRITE_MAX text bytes at address. Control bytes other than tab are
 * logged as '?'; the kernel stamps the caller's space, whatever the text says. A
 * line beyond the space's rate is dropped and still returns success.
 * No reply. */
struct log_write_request {
  struct message_header header;
  uint64_t level;
  uint64_t address;
  uint64_t length;
};

/* LOG_STREAM needs LOG_RIGHT_WRITE and returns a new handle to a write-only
 * console-protocol stream: whatever is written is assembled into lines of at most
 * LOG_WRITE_MAX bytes (ended by '\n' or by that length) and logged at level as
 * the creating process's space. It carries only CONSOLE WRITE authority, so a
 * program may use it as stdout or stderr. SIZE and READ are refused. The final
 * close logs an unfinished line. */
struct log_stream_request {
  struct message_header header;
  uint64_t level;
};

struct log_stream_reply {
  handle_t handle;
};

_Static_assert(sizeof(struct log_cursor) == 16, "log cursor layout");
_Static_assert(sizeof(struct log_snapshot) == 48, "log snapshot layout");
_Static_assert(sizeof(struct log_read_request) == 48, "log read request layout");
_Static_assert(sizeof(struct log_read_reply) == 32, "log read reply layout");
_Static_assert(sizeof(struct log_record) == 4, "log record layout");
_Static_assert(sizeof(struct log_write_request) == 40, "log write request layout");
_Static_assert(sizeof(struct log_stream_request) == 24, "log stream request layout");

#endif
