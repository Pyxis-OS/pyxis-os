#ifndef ABI_TERMINAL_H
#define ABI_TERMINAL_H

#include <abi/handle.h>
#include <abi/message.h>
#include <stddef.h>

#define TERMINAL_SERVICE_RIGHT_CREATE (UINT64_C(1) << 0)
#define TERMINAL_CREATE UINT64_C(1)
#define TERMINAL_RIGHT_INJECT (UINT64_C(1) << 0)
#define TERMINAL_RIGHT_DRAIN (UINT64_C(1) << 1)
#define TERMINAL_RIGHT_HANGUP (UINT64_C(1) << 2)
#define TERMINAL_RIGHTS (TERMINAL_RIGHT_INJECT | TERMINAL_RIGHT_DRAIN | TERMINAL_RIGHT_HANGUP)
#define TERMINAL_TRY_INJECT UINT64_C(1)
#define TERMINAL_TRY_DRAIN UINT64_C(2)
#define TERMINAL_END_INPUT UINT64_C(3)
#define TERMINAL_HANGUP UINT64_C(4)
#define TERMINAL_EVENTS_RIGHT_EMIT (UINT64_C(1) << 0)
#define TERMINAL_COMMAND_COMPLETE UINT64_C(1)

#define TERMINAL_INPUT_CAPACITY 4096
#define TERMINAL_OUTPUT_CAPACITY 65536
#define TERMINAL_TRANSFER_MAX 4096
#define TERMINAL_COLUMNS_MAX 512
#define TERMINAL_ROWS_MAX 256
#define TERMINAL_RECORD_DATA UINT64_C(1)
#define TERMINAL_RECORD_FRESH_LINE UINT64_C(2)
#define TERMINAL_RECORD_TAB_WIDTH UINT64_C(3)
#define TERMINAL_RECORD_COMMAND_COMPLETE UINT64_C(4)

struct terminal_create_request {
  struct message_header header;
  uint64_t columns;
  uint64_t rows;
};

/* All four handles or none. Input/output implement CONSOLE with READ/WRITE
 * respectively. Attachment authority is never implied by application handles.
 * Events implement TERMINAL_EVENTS with EMIT; delegate separately to the root
 * shell, without forwarding to commands. Events are not a standard stream.
 * Dimensions are immutable, nonzero and bounded by the constants above. */
struct terminal_create_reply {
  handle_t input;
  handle_t output;
  handle_t attachment;
  handle_t events;
};

/* EMIT authority; status is 0 (success) or 1 (failure), otherwise BAD_REQUEST.
 * No reply payload. Inserts one indivisible COMMAND_COMPLETE output record,
 * ordered with DATA and controls. Capacity backpressure waits interruptibly;
 * hangup or execution-group stop returns ENDPOINT_CLOSED. */
struct terminal_command_complete_request {
  struct message_header header;
  uint64_t status;
};

/* The kernel assigns command numbers starting at 1 in enqueue order. Failed
 * emits consume no number; exhausted numbering returns LIMIT without wrapping.
 * These are native uint64_t fields, not a wire encoding. */
struct terminal_command_complete {
  uint64_t command;
  uint64_t status;
};

struct terminal_transfer_request {
  struct message_header header;
  uint64_t buffer;
  uint64_t length; /* Input length for INJECT, output capacity for DRAIN. */
};
struct terminal_transfer_reply {
  uint64_t length;
};

/* DRAIN returns exactly one whole record, including this header. Length is the
 * payload size: DATA 1..4096, FRESH_LINE zero, TAB_WIDTH one uint64_t (1..32),
 * COMMAND_COMPLETE one struct terminal_command_complete.
 * BUFFER_TOO_SMALL consumes nothing. A successful zero-byte drain means output
 * EOF, after all queued records and the last WRITE/EMIT authorities close. */
struct terminal_record {
  uint64_t type;
  uint64_t length;
};
#define TERMINAL_RECORD_MAX (sizeof(struct terminal_record) + TERMINAL_TRANSFER_MAX)

/* TRY_INJECT needs INJECT, TRY_DRAIN needs DRAIN. They never wait for queue
 * readiness. A full input queue or empty live output queue returns WOULD_BLOCK,
 * with no partial operation left pending. INJECT may accept a short prefix.
 * Zero-length INJECT is a validated no-op; DRAIN requires room for a header.
 * Failure leaves reply/data buffers untouched. No retained user pointers.
 *
 * END_INPUT needs INJECT: it is idempotent and preserves queued input, then
 * application reads return EOF. Later nonempty injection fails ENDPOINT_CLOSED.
 * HANGUP needs HANGUP: permanently discard both queues, wake blocked operations
 * with ENDPOINT_CLOSED. It is idempotent. Losing the last HANGUP-authorized
 * attachment grant also hangs up, regardless of observation/storage references.
 * Application input/output closure is tracked independently of internal refs.
 * EMIT grants, including captured IPC grants, keep output EOF open alongside
 * application WRITE grants. Final EMIT closure neither hangs up nor terminates.
 * Waiting uses DRAIN for READABLE/PEER_FIN (output EOF), INJECT for WRITABLE/
 * WRITE_CLOSED (input closure). Relevant hangup automatically reports ERROR.
 * No terminal operation terminates processes. */

_Static_assert(sizeof(struct terminal_create_request) == 32, "terminal create layout");
_Static_assert(sizeof(struct terminal_create_reply) == 32, "terminal create reply layout");
_Static_assert(sizeof(struct terminal_command_complete_request) == 24, "terminal event request layout");
_Static_assert(offsetof(struct terminal_command_complete_request, status) == 16, "terminal event status offset");
_Static_assert(sizeof(struct terminal_command_complete) == 16, "terminal completion layout");
_Static_assert(sizeof(struct terminal_transfer_request) == 32, "terminal transfer layout");
_Static_assert(sizeof(struct terminal_transfer_reply) == 8, "terminal transfer reply layout");
_Static_assert(sizeof(struct terminal_record) == 16, "terminal record layout");

#endif
