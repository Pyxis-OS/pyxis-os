#ifndef ABI_CONSOLE_H
#define ABI_CONSOLE_H

#include <abi/message.h>
#include <stddef.h>

#define CONSOLE_RIGHT_WRITE (UINT64_C(1) << 0)
#define CONSOLE_RIGHT_READ (UINT64_C(1) << 1)
#define CONSOLE_RIGHTS (CONSOLE_RIGHT_WRITE | CONSOLE_RIGHT_READ)

#define CONSOLE_WRITE UINT64_C(1)
#define CONSOLE_READ UINT64_C(2)
#define CONSOLE_SIZE UINT64_C(3)
/* WRITE authority; ignored fixed-size payload, no reply. Ends an incomplete
 * escape sequence and emits a newline only when the cursor is not at column 0. */
#define CONSOLE_FRESH_LINE UINT64_C(4)
#define CONSOLE_SET_TAB_WIDTH UINT64_C(5)
#define CONSOLE_TAB_WIDTH_MIN UINT64_C(1)
#define CONSOLE_TAB_WIDTH_MAX UINT64_C(32)
#define CONSOLE_WAIT_FOREVER UINT64_MAX

struct console_write_request {
  uint64_t address;
  uint64_t length;
};

/* READ waits for available bytes, without echo or editing. Independent terminal
 * sessions return zero after their input queue drains following END_INPUT.
 * Framebuffer consoles have no input-EOF operation. Hangup is ENDPOINT_CLOSED.
 * Zero capacity succeeds immediately. INPUT_LOST acknowledges discarded input;
 * retry starts a fresh stream. Navigation sequences may span short reads.
 * timeout_ms is 0 for a poll, 1..UINT32_MAX for a bounded wait, or WAIT_FOREVER.
 * The budget includes waiting behind another reader. Expiry returns TIMED_OUT
 * without a reply or consuming bytes. Available input/ownership wins a race
 * with expiry when observed under the input lock. Deadlines use monotonic
 * elapsed time; scheduling may delay wakeup. VM pause time need not count. */
struct console_read_request {
  uint64_t address;
  uint64_t capacity;
  uint64_t timeout_ms;
};

/* WRITE authority; no reply. Width is 1..32 columns, otherwise BAD_REQUEST.
 * Changes subsequent tabs on this console's TTY, shared by its writers.
 * Does not move the cursor, redraw text or reset pending wrap/parser state. */
struct console_tab_width_request {
  uint64_t columns;
};

union console_payload {
  struct console_write_request write;
  struct console_read_request read;
  struct console_tab_width_request tab_width;
};

struct console_message {
  struct message_header header;
  union console_payload body;
};

struct console_write_reply {
  uint64_t written;
};

struct console_read_reply {
  uint64_t read;
};

/* Character cells, excluding session navigation. SIZE needs READ or WRITE;
 * its fixed-size payload is ignored. No resize notifications yet. */
struct console_size_reply {
  uint64_t columns;
  uint64_t rows;
};

_Static_assert(sizeof(struct console_read_request) == 24, "console read layout");
_Static_assert(sizeof(struct console_tab_width_request) == 8, "console tab width layout");
_Static_assert(sizeof(struct console_read_reply) == 8, "console read reply layout");
_Static_assert(sizeof(struct console_size_reply) == 16, "console size reply layout");
_Static_assert(sizeof(struct console_write_request) == 16, "console request layout");
_Static_assert(sizeof(struct console_write_reply) == 8, "console reply layout");
_Static_assert(offsetof(struct console_message, body) == 16, "console payload offset");
_Static_assert(sizeof(struct console_message) == 40, "console message layout");

#endif
