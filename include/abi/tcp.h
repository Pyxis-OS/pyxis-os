#ifndef ABI_TCP_H
#define ABI_TCP_H

#include <abi/handle.h>
#include <abi/message.h>
#include <abi/syscall.h>

#define TCP_SERVICE_RIGHT_CONNECT (UINT64_C(1) << 0)
#define TCP_CONNECT UINT64_C(1)
#define TCP_CONNECT_MAX_WAIT_NS UINT64_C(30000000000)

#define TCP_RIGHT_INSPECT (UINT64_C(1) << 0)
#define TCP_RIGHT_ABORT (UINT64_C(1) << 1)
#define TCP_RIGHT_READ (UINT64_C(1) << 2)
#define TCP_RIGHT_WRITE (UINT64_C(1) << 3)
#define TCP_RIGHTS (TCP_RIGHT_INSPECT | TCP_RIGHT_ABORT | TCP_RIGHT_READ | TCP_RIGHT_WRITE)
#define TCP_INSPECT UINT64_C(1)
#define TCP_ABORT UINT64_C(2)
#define TCP_READ UINT64_C(3)
#define TCP_WRITE UINT64_C(4)
#define TCP_READ_MAX_BYTES UINT64_C(4096)
#define TCP_READ_MAX_WAIT_NS UINT64_C(30000000000)
#define TCP_WRITE_MAX_BYTES UINT64_C(4096)
#define TCP_WRITE_MAX_WAIT_NS UINT64_C(30000000000)

#define TCP_STATE_CONNECTED UINT32_C(1)
#define TCP_STATE_PEER_CLOSED UINT32_C(2)
#define TCP_STATE_CLOSED UINT32_C(3)

/* PROTOCOL_TCP_SERVICE: numeric, host-order IPv4/port and an absolute monotonic
 * deadline, at most 30 seconds ahead. Nonzero peer port; no source selection.
 * CONNECT returns TCP_RIGHTS only after a handshake, never a partial handle.
 * Failure leaves reply storage untouched. Eight shared control slots include
 * queued, connecting and completed calls until their callers resume. */
struct tcp_connect_request {
  struct message_header header;
  uint32_t address;
  uint16_t port;
  uint16_t reserved;
  uint64_t deadline_ns;
};

struct tcp_connection_info {
  uint32_t local_address;
  uint32_t remote_address;
  uint16_t local_port;
  uint16_t remote_port;
  uint32_t state;
  uint32_t terminal_status; /* call_status; CALL_OK includes an orderly peer FIN. */
  uint32_t reserved;
};

struct tcp_connect_reply {
  handle_t handle;
  struct tcp_connection_info connection;
};

/* PROTOCOL_TCP: header-only INSPECT/ABORT with separate rights. INSPECT returns
 * tcp_connection_info even after failure. ABORT is idempotent, affects copies,
 * and returns no bytes; the first terminal failure remains visible. Final close
 * also aborts. Closing one copy does not stop a connection owned by other copies.
 * No write-shutdown operation is exposed yet. */

/* READ clamps capacity to TCP_READ_MAX_BYTES and returns available ordered
 * bytes, possibly short. A nonempty read returns zero only after peer FIN and
 * all preceding bytes. Zero capacity is a no-op after authority/deadline checks.
 * Deadline is absolute monotonic time, at most 30 seconds ahead. Timeout/error
 * transfers nothing and leaves both outputs untouched. The effective data range
 * and reply must be writable and nonoverlapping. Reset/abort discard unread
 * bytes; a successful read already completed before that event stays successful.
 * One outstanding READ per shared stream (otherwise BUSY); sixteen globally,
 * including completed replies until collected by their callers. */
struct tcp_read_request {
  struct message_header header;
  uint64_t buffer;
  uint64_t capacity;
  uint64_t deadline_ns;
};

struct tcp_read_reply {
  uint64_t length;
};

/* WRITE clamps length to TCP_WRITE_MAX_BYTES. Success reports bytes copied into
 * bounded send storage, not peer acknowledgment or application delivery. Retry
 * only the unaccepted suffix. Nonempty success always has a positive count.
 * Deadline is absolute monotonic time, at most 30 seconds ahead. A blocked call
 * times out without accepting bytes or changing its reply; earlier writes stay
 * queued. Zero length is a no-op after authority/buffer/deadline validation.
 * One outstanding writer per shared stream (otherwise BUSY), sixteen globally
 * including completed replies. READ proceeds independently; peer FIN permits
 * further writes. Final close still aborts and is not a flush. */
struct tcp_write_request {
  struct message_header header;
  uint64_t buffer;
  uint64_t length;
  uint64_t deadline_ns;
};

struct tcp_write_reply {
  uint64_t length;
};

_Static_assert(sizeof(struct tcp_write_request) == 40, "TCP write request");
_Static_assert(sizeof(struct tcp_write_reply) == 8, "TCP write reply");
_Static_assert(sizeof(struct tcp_read_request) == 40, "TCP read request");
_Static_assert(sizeof(struct tcp_read_reply) == 8, "TCP read reply");
_Static_assert(sizeof(struct tcp_connect_request) == 32, "TCP connect request");
_Static_assert(sizeof(struct tcp_connection_info) == 24, "TCP connection info");
_Static_assert(sizeof(struct tcp_connect_reply) == 32, "TCP connect reply");

#endif
