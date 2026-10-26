#ifndef ABI_ENDPOINT_H
#define ABI_ENDPOINT_H

#include <abi/message.h>
#include <abi/handle.h>
#include <stddef.h>

#define ENDPOINT_SERVICE_RIGHT_CREATE (UINT64_C(1) << 0)
#define ENDPOINT_CREATE UINT64_C(1)
#define ENDPOINT_EXPORT UINT64_C(2)
#define ENDPOINT_RECEIVER_RIGHT_CONTROL (UINT64_C(1) << 0)
#define ENDPOINT_RIGHT_REPLY (UINT64_C(1) << 0)
#define ENDPOINT_RIGHT_COLLECT (UINT64_C(1) << 1)
#define ENDPOINT_CALL UINT64_C(1)
#define ENDPOINT_SEND UINT64_C(2)
#define ENDPOINT_CALL_BULK UINT64_C(3)
#define ENDPOINT_RECEIVE UINT64_C(1)
#define ENDPOINT_RETIRE_ACK UINT64_C(3)
#define ENDPOINT_TRY_RECEIVE UINT64_C(4)
#define ENDPOINT_REPLY UINT64_C(1)
#define ENDPOINT_REPLY_BULK UINT64_C(2)
#define ENDPOINT_COLLECT_BULK UINT64_C(3)
#define ENDPOINT_DATA_MAX 4096
#define ENDPOINT_GRANTS_MAX 4
#define ENDPOINT_DELIVERIES_MAX 16
#define ENDPOINT_EXPORTS_MAX 64

struct endpoint_grant {
  handle_t handle;
  uint64_t rights;
  uint64_t transport;
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

/* Exports are created through the service using an owned receiver. The receiver
 * retains retirement authority. Each export returns a revocable client and its
 * CUT controller. IDs are nonzero and unique until retirement acknowledgment.
 * Closing the receiver shuts down all exports. CUT denies queued work; received
 * work retains its receipt and may complete. Controller ownership does not keep
 * provider-private state alive after the last client and in-flight operation.
 * Allowed rights are protocol-specific; transport is SEND and/or RECEIVE. */
struct endpoint_export_message {
  struct message_header header;
  handle_t receiver;
  uint64_t object_id;
  uint64_t protocol;
  uint64_t rights;
  uint64_t transport;
};

struct endpoint_export_reply {
  handle_t client;
  handle_t controller;
};

struct endpoint_control_message {
  struct message_header header;
  uint64_t object_id;
};

/* CALL, SEND and REPLY use caller-local bytes; these addresses are never delivered.
 * REPLY invokes the receipt, not the receiving endpoint. result is an opaque
 * application result on REPLY and must be zero on CALL/SEND. Unused grants are zero.
 * deadline_ns is an absolute monotonic CALL deadline, zero for unlimited.
 * SEND and REPLY must supply zero. RECEIVE, TRY_RECEIVE and CREATE take only a message_header
 * with their own protocol. */
struct endpoint_message {
  struct message_header header;
  uint64_t buffer;
  uint64_t size;
  uint64_t grant_count;
  uint64_t result;
  uint64_t deadline_ns;
  uint64_t protocol; /* Export protocol; zero for raw endpoint delivery/reply. */
  uint64_t operation; /* Provider operation; zero for raw endpoint delivery/reply. */
  struct endpoint_grant grants[ENDPOINT_GRANTS_MAX];
};

/* CALL_BULK uses endpoint_message and requires the exported FILE's READ rights
 * and CALL transport, like ordinary FILE READ. protocol is FILE, operation is
 * FILE_READ_BULK, size is exactly sizeof(file_bulk_read_request), and buffer
 * addresses only that caller-local request. Inner/outer deadlines must match;
 * capacity must fit FILE_READ_BULK_MAX_BYTES and the raw syscall reply capacity.
 * grant_count, grants and result are zero. Success returns raw bytes and their
 * count as syscall reply_size, without an endpoint_packet/count prefix. Errors
 * provide no usable count. Caller output stays owned/reserved until return;
 * only the actual initialized extent is copied, with no writes after return.
 * Ordinary CALL packet and delivery-state reporting remain specific to CALL.
 *
 * REPLY_BULK invokes a bulk CALL receipt with REPLY authority using the same
 * endpoint_message. buffer/size name provider-local raw bytes; size cannot
 * exceed the admitted capacity. result is a native call_status; an error requires
 * size zero and returns no usable count. No attachments are allowed. protocol, operation,
 * deadline_ns and unused grants are zero, as for ordinary REPLY. A successful
 * reply consumes the receipt; invalid replies leave it live. The provider
 * borrows its input until return, and cannot expose any unused buffer tail.
 *
 * COLLECT_BULK invokes a native completion receipt with COLLECT authority using
 * only a message_header. The small completion payload is file_read_reply;
 * bulk data is separate. The syscall reply buffer receives raw bytes and
 * reply_size reports their count. Invalid/short output leaves data available
 * for retry without repeating I/O; successful collection consumes collection
 * authority and discards its data. CLOSE discards any uncollected data and
 * acknowledges the completion. Ordinary completion receipts gain no COLLECT
 * right; receipt grants remain noncopyable, nontransferable and uninherited. */

enum endpoint_message_kind {
  ENDPOINT_MESSAGE_CALL = 1,
  ENDPOINT_MESSAGE_SEND = 2,
  ENDPOINT_MESSAGE_CANCEL = 3,
  ENDPOINT_MESSAGE_RETIRE = 4,
  ENDPOINT_MESSAGE_COMPLETION = 5,
};

enum endpoint_delivery {
  ENDPOINT_NOT_DELIVERED = 0,
  ENDPOINT_DELIVERED = 1,
};

/* RECEIVE supplies a single-use receipt and a kernel-authenticated kind.
 * TRY_RECEIVE uses the same owned receiver/RECEIVE transport and selection,
 * collection and receipt rules. Empty open receivers return WOULD_BLOCK without
 * parking for a message; closure returns ENDPOINT_CLOSED. Readiness reserves
 * nothing, so multiplexed providers use TRY after a readiness scan.
 * RETIRE has no receipt, grants or payload. It identifies the export whose
 * provider state is no longer in use. ACK on the receiver releases that ID.
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
 * deadline, possibly zero for closure. reason distinguishes timeout/closure. A notice consumes no delivery slot. A valid late REPLY on an open
 * endpoint returns CALL_TIMED_OUT without consuming the receipt; CLOSE releases
 * it and any pending notice. Validation and endpoint-closure errors still apply.
 * Delivered attachments remain owned independently of cancellation. */
/* COMPLETION joins ordinary FIFO only after native worker ownership drains.
 * result is the native status; reason and completion_flags retain independent
 * cancellation/progress information. Its zero-right receipt's CLOSE acknowledges
 * the operation. RECEIVE uses preclaimed slots: invalid or short output leaves
 * the result and grants queued for retry without repeating native work. */
/* Bulk completions additionally grant COLLECT on their receipt, as described above. */
struct endpoint_packet {
  handle_t receipt;
  uint64_t delivery;
  uint64_t kind;
  uint64_t result;
  uint64_t size;
  uint64_t grant_count;
  uint64_t deadline_ns;
  uint64_t object_id;
  uint64_t protocol;
  uint64_t operation;
  uint64_t rights; /* Actual invoking grant, not the export ceiling. */
  uint64_t reason; /* CANCEL or COMPLETION cancellation/deadline reason. */
  uint64_t correlation_id; /* COMPLETION: native submission identity. */
  uint64_t completion_flags;
  uint64_t progress;
  struct endpoint_grant grants[ENDPOINT_GRANTS_MAX];
  uint8_t data[ENDPOINT_DATA_MAX];
};

#define ENDPOINT_PACKET_HEADER_SIZE offsetof(struct endpoint_packet, data)

_Static_assert(sizeof(struct endpoint_export_message) == 56, "endpoint export layout");
_Static_assert(sizeof(struct endpoint_control_message) == 24, "endpoint control layout");
_Static_assert(sizeof(struct endpoint_message) == 168, "endpoint message layout");
_Static_assert(ENDPOINT_PACKET_HEADER_SIZE == 216, "endpoint packet layout");
_Static_assert(sizeof(struct endpoint_create_reply) == 16, "endpoint create layout");
_Static_assert(sizeof(struct endpoint_export_reply) == 16, "endpoint export reply layout");

#endif
