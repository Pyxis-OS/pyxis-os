#ifndef ABI_CONSOLE_H
#define ABI_CONSOLE_H

#include <abi/handle.h>
#include <abi/message.h>
#include <stddef.h>

#define CONSOLE_RIGHT_WRITE (UINT64_C(1) << 0)
#define CONSOLE_RIGHT_READ (UINT64_C(1) << 1)
#define CONSOLE_RIGHTS (CONSOLE_RIGHT_WRITE | CONSOLE_RIGHT_READ)
/* Input only; authorizes ARM_INTERRUPT. Never valid on a standard stream. */
#define CONSOLE_RIGHT_INTERRUPT (UINT64_C(1) << 2)
/* Each alone on a handle returned by ARM_INTERRUPT or PASSTHROUGH. */
#define CONSOLE_RIGHT_ARMED (UINT64_C(1) << 3)
#define CONSOLE_RIGHT_PASSTHROUGH (UINT64_C(1) << 4)

#define CONSOLE_WRITE UINT64_C(1)
#define CONSOLE_READ UINT64_C(2)
#define CONSOLE_SIZE UINT64_C(3)
/* WRITE authority; ignored fixed-size payload, no reply. Ends an incomplete
 * escape sequence and emits a newline only when the cursor is not at column 0. */
#define CONSOLE_FRESH_LINE UINT64_C(4)
#define CONSOLE_SET_TAB_WIDTH UINT64_C(5)
#define CONSOLE_TAB_WIDTH_MIN UINT64_C(1)
#define CONSOLE_TAB_WIDTH_MAX UINT64_C(32)
#define CONSOLE_ARM_INTERRUPT UINT64_C(6)
#define CONSOLE_PASSTHROUGH UINT64_C(7)

#define CONSOLE_PASTE_REGISTER UINT64_C(8)
#define CONSOLE_PASTE_READ UINT64_C(9)
#define CONSOLE_PASTE_ACK UINT64_C(10)
#define CONSOLE_PASTE_RELEASE UINT64_C(11)
#define CONSOLE_TRY_WRITE UINT64_C(12)
/* ARMED authority; header-only request, no reply. Consumes one pending
 * interrupt, returning WOULD_BLOCK if none. The input remains armed. */
#define CONSOLE_ACK_INTERRUPT UINT64_C(13)
#define CONSOLE_DRAIN UINT64_C(14)
#define CONSOLE_FLUSH_INPUT UINT64_C(15)
#define CONSOLE_DRAIN_TIMEOUT_MAX_MS UINT64_C(30000)

/* READ or WRITE authority. Fence the terminal output prefix queued when the
 * operation starts. Success means complete records were copied to a DRAIN
 * attachment, not forwarded over a network or presented. Later writes do not
 * extend the fence. Local framebuffer writes already apply to the TTY, so
 * their drain completes immediately. No reply. Timeout leaves output intact;
 * detach/no DRAIN attachment or endpoint closure returns ENDPOINT_CLOSED.
 * timeout_ms is 0 for a poll or 1..CONSOLE_DRAIN_TIMEOUT_MAX_MS for a bounded
 * wait. A ready fence wins over timeout; caller stop/revocation still unwinds.
 */
struct console_drain_request {
  uint64_t timeout_ms;
};

/* FLUSH_INPUT needs READ. Discard queued ordinary input at one instant,
 * including an acknowledged framebuffer input-loss condition, without waiting
 * for new input. EOF/hangup, raw holds, interrupt and clipboard framing are
 * unchanged. Another active reader or paste receiver can return BUSY.
 * Takes the existing ignored fixed-size payload; no reply. */

#define CONSOLE_PASTE_INPUT UINT64_C(1)
#define CONSOLE_PASTE_BEGIN UINT64_C(2)
#define CONSOLE_PASTE_DATA UINT64_C(3)
#define CONSOLE_PASTE_END UINT64_C(4)
#define CONSOLE_PASTE_CANCEL UINT64_C(5)
#define CONSOLE_PASTE_BOUNDARY UINT64_C(1)

/* Process-owned opt-in; READ plus matching output WRITE in the same space.
 * Epochs never transfer with a grant. Ordinary INPUT returns exactly one byte;
 * DATA may return a short prefix. Framing has no ordinary FIFO dependency.
 * A read atomically declares its decoder boundary; INPUT closes it before
 * handing off any byte. ACK follows consumed END/CANCEL, never just receipt. */
struct console_paste_register_request {
  struct message_header header;
  handle_t output;
};

struct console_paste_register_reply {
  uint64_t epoch;
};

struct console_paste_read_request {
  struct message_header header;
  uint64_t epoch;
  uint64_t address, capacity;
  uint64_t timeout_ms, flags;
};

struct console_paste_read_reply {
  uint64_t kind, epoch, transaction_id;
  uint64_t length, status;
};

struct console_paste_ack_request {
  struct message_header header;
  uint64_t epoch, transaction_id;
};

struct console_paste_release_request {
  struct message_header header;
  uint64_t epoch;
};

#define CONSOLE_WAIT_FOREVER UINT64_MAX

struct console_write_request {
  uint64_t address;
  uint64_t length;
};

/* TRY_WRITE uses WRITE authority and the same fixed-size payload as WRITE.
 * A nonempty request makes bounded positive progress or returns WOULD_BLOCK
 * without output. Independent sessions admit one short DATA record, counting
 * its header against queue capacity; hangup/closed output is ENDPOINT_CLOSED.
 * Framebuffer consoles render synchronously under the shared output lock;
 * unavailable output is UNAVAILABLE.
 * A validated zero-length request succeeds without probing output liveness.
 * Try operations never wait for I/O readiness, but may serialize output. */
struct console_try_write_reply {
  uint64_t written;
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

/* Ctrl+C interruption of console and terminal text input. ARM_INTERRUPT and
 * PASSTHROUGH take the ignored fixed-size payload and return one new handle
 * to the same input object.
 *
 * ARM_INTERRUPT needs INTERRUPT and returns a handle carrying only ARMED. The
 * input stays armed while any ARMED grant exists; closing the last one disarms
 * and clears pending interrupts. Arming while armed is BUSY; BUSY can also be
 * transient while another ARM is installing its handle. Each armed interval
 * starts with no pending interrupts. The ARMED handle supports ACK_INTERRUPT.
 *
 * PASSTHROUGH needs READ and returns a handle carrying only PASSTHROUGH. While
 * any PASSTHROUGH grant exists, Ctrl+C is ordinary input.
 *
 * While armed without passthrough, each byte 3 from keyboard text or terminal
 * injection is removed from input, adds one pending interrupt up to two, and
 * discards input queued before it, including a pending input loss; later bytes
 * are kept. Raw keyboard events are unaffected.
 * Unarmed or passthrough byte 3 is data. wait_many WAIT_INTERRUPT on an ARMED
 * handle reports pending interrupts, level-triggered and not consumed;
 * terminal hangup reports WAIT_ERROR. */
struct console_handle_reply {
  handle_t handle;
};

union console_payload {
  struct console_write_request write;
  struct console_read_request read;
  struct console_tab_width_request tab_width;
  struct console_drain_request drain;
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
 * its fixed-size payload is ignored. Geometry and generation are one snapshot.
 * Generation starts at 1; local consoles advance it on committed screen resize.
 * Independent sessions advance on explicit attachment-authorized resize.
 * wait_many RESIZED compares an interest's observed generation with this value. */
struct console_size_reply {
  uint64_t columns;
  uint64_t rows;
  uint64_t generation;
};

_Static_assert(sizeof(struct console_paste_register_request) == 24, "paste register layout");
_Static_assert(sizeof(struct console_paste_register_reply) == 8, "paste epoch layout");
_Static_assert(sizeof(struct console_paste_read_request) == 56, "paste read layout");
_Static_assert(sizeof(struct console_paste_read_reply) == 40, "paste read reply layout");
_Static_assert(sizeof(struct console_paste_ack_request) == 32, "paste ack layout");
_Static_assert(sizeof(struct console_paste_release_request) == 24, "paste release layout");

_Static_assert(sizeof(struct console_handle_reply) == 8, "console handle reply layout");
_Static_assert(sizeof(struct console_read_request) == 24, "console read layout");
_Static_assert(sizeof(struct console_tab_width_request) == 8, "console tab width layout");
_Static_assert(sizeof(struct console_read_reply) == 8, "console read reply layout");
_Static_assert(sizeof(struct console_size_reply) == 24, "console size reply layout");
_Static_assert(sizeof(struct console_write_request) == 16, "console request layout");
_Static_assert(sizeof(struct console_write_reply) == 8, "console reply layout");
_Static_assert(sizeof(struct console_try_write_reply) == 8, "console try write reply layout");
_Static_assert(offsetof(struct console_message, body) == 16, "console payload offset");
_Static_assert(sizeof(struct console_message) == 40, "console message layout");

#endif
