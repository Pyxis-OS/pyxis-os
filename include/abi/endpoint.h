#ifndef ABI_ENDPOINT_H
#define ABI_ENDPOINT_H

#include <abi/message.h>
#include <abi/handle.h>
#include <stddef.h>

#define ENDPOINT_SERVICE_RIGHT_CREATE (UINT64_C(1) << 0)
#define ENDPOINT_CREATE UINT64_C(1)
#define ENDPOINT_RIGHT_SEND (UINT64_C(1) << 0)
#define ENDPOINT_RIGHT_RECEIVE (UINT64_C(1) << 1)
#define ENDPOINT_RIGHT_REPLY (UINT64_C(1) << 0)
#define ENDPOINT_CALL UINT64_C(1)
#define ENDPOINT_SEND UINT64_C(2)
#define ENDPOINT_RECEIVE UINT64_C(1)
#define ENDPOINT_REPLY UINT64_C(1)
#define ENDPOINT_DATA_MAX 4096
#define ENDPOINT_GRANTS_MAX 4
#define ENDPOINT_DELIVERIES_MAX 16

struct endpoint_grant {
  handle_t handle;
  uint64_t rights;
};

/* Client SEND admits a one-way message; CALL requires SEND | RECEIVE, where
 * RECEIVE authorizes only that call's reply. A RECEIVE-only client grant has
 * no standalone operation. The owned receiver uses RECEIVE for incoming work.
 *
 * Creation returns both handles or neither. The receiver belongs to its
 * creating process and cannot be copied, transferred or inherited. Clients
 * can copy the client handle with SEND and/or RECEIVE. Creation gives both.
 * Owner exit or receiver close shuts it down. */
struct endpoint_create_reply {
  handle_t receiver;
  handle_t caller;
};

/* CALL, SEND and REPLY use caller-local bytes; these addresses are never delivered.
 * REPLY invokes the receipt, not the receiving endpoint. result is an opaque
 * application result on REPLY and must be zero on CALL/SEND. Unused grants are zero.
 * deadline_ns is an absolute monotonic CALL deadline, zero for unlimited.
 * SEND and REPLY must supply zero. RECEIVE and CREATE take only a message_header
 * with their own protocol. */
struct endpoint_message {
  struct message_header header;
  uint64_t buffer;
  uint64_t size;
  uint64_t grant_count;
  uint64_t result;
  uint64_t deadline_ns;
  struct endpoint_grant grants[ENDPOINT_GRANTS_MAX];
};

enum endpoint_message_kind {
  ENDPOINT_MESSAGE_CALL = 1,
  ENDPOINT_MESSAGE_SEND = 2,
  ENDPOINT_MESSAGE_CANCEL = 3,
};

enum endpoint_delivery {
  ENDPOINT_NOT_DELIVERED = 0,
  ENDPOINT_DELIVERED = 1,
};

/* RECEIVE supplies a single-use receipt and a kernel-authenticated kind.
 * CALL receipts carry reply authority; SEND receipts carry zero operation rights.
 * Successful REPLY consumes a CALL receipt; invalid replies leave it live.
 * CLOSE finishes a SEND receipt or abandons an unanswered CALL. Receipt grants are
 * never copyable, transferable or inherited. Separately delivered grants remain
 * owned by their recipient regardless of receipt completion.
 *
 * CALL returns this prefix even on transport failure after output validation,
 * preserving delivery state separately from syscall status. result is meaningful
 * only on successful transport. reply_size is offsetof(data) + size, not the
 * whole packet. Only the supplied payload bytes are copied.
 * SEND returns status only: success means admission, with no later completion
 * report. Accepted sends survive sender close/exit and share CALL's sixteen
 * delivery slots and FIFO. RECEIVE alone does not release a send's slot.
 *
 * CALL expiry returns CALL_TIMED_OUT with delivery state. A delivered timeout
 * does not prove whether the operation ran. RECEIVE prioritizes CANCEL notices
 * over ordinary messages: receipt identifies an existing owned receipt, not a
 * new grant; size, grant_count and result are zero. deadline_ns is the original
 * deadline. A notice consumes no delivery slot. A valid late REPLY on an open
 * endpoint returns CALL_TIMED_OUT without consuming the receipt; CLOSE releases
 * it and any pending notice. Validation and endpoint-closure errors still apply.
 * Delivered attachments remain owned independently of cancellation. */
struct endpoint_packet {
  handle_t receipt;
  uint64_t delivery;
  uint64_t kind;
  uint64_t result;
  uint64_t size;
  uint64_t grant_count;
  uint64_t deadline_ns;
  struct endpoint_grant grants[ENDPOINT_GRANTS_MAX];
  uint8_t data[ENDPOINT_DATA_MAX];
};

#define ENDPOINT_PACKET_HEADER_SIZE offsetof(struct endpoint_packet, data)

_Static_assert(sizeof(struct endpoint_message) == 120, "endpoint message layout");
_Static_assert(ENDPOINT_PACKET_HEADER_SIZE == 120, "endpoint packet layout");
_Static_assert(sizeof(struct endpoint_create_reply) == 16, "endpoint create layout");

#endif
