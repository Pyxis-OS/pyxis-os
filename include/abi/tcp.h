#ifndef ABI_TCP_H
#define ABI_TCP_H

#include <abi/handle.h>
#include <abi/message.h>
#include <abi/syscall.h>

#define TCP_SERVICE_RIGHT_CONNECT (UINT64_C(1) << 0)
#define TCP_SERVICE_RIGHT_LISTEN (UINT64_C(1) << 1)
#define TCP_SERVICE_RIGHTS (TCP_SERVICE_RIGHT_CONNECT | TCP_SERVICE_RIGHT_LISTEN)
#define TCP_CONNECT UINT64_C(1)
#define TCP_LISTEN UINT64_C(2)
#define TCP_CONNECT_MAX_WAIT_NS UINT64_C(30000000000)

#define TCP_LISTENER_RIGHT_INSPECT (UINT64_C(1) << 0)
#define TCP_LISTENER_RIGHT_ACCEPT (UINT64_C(1) << 1)
#define TCP_LISTENER_RIGHTS (TCP_LISTENER_RIGHT_INSPECT | TCP_LISTENER_RIGHT_ACCEPT)
#define TCP_LISTENER_INSPECT UINT64_C(1)
#define TCP_ACCEPT UINT64_C(2)
#define TCP_ACCEPT_MAX_WAIT_NS UINT64_C(30000000000)
#define TCP_LISTENER_LIMIT UINT32_C(4)
#define TCP_LISTENER_PENDING_LIMIT UINT32_C(4)
#define TCP_LISTENER_HANDSHAKE_NS UINT64_C(10000000000)

#define TCP_RIGHT_INSPECT (UINT64_C(1) << 0)
#define TCP_RIGHT_ABORT (UINT64_C(1) << 1)
#define TCP_RIGHT_READ (UINT64_C(1) << 2)
#define TCP_RIGHT_WRITE (UINT64_C(1) << 3)
#define TCP_RIGHT_SHUTDOWN_WRITE (UINT64_C(1) << 4)
#define TCP_RIGHTS (TCP_RIGHT_INSPECT | TCP_RIGHT_ABORT | TCP_RIGHT_READ | TCP_RIGHT_WRITE | \
    TCP_RIGHT_SHUTDOWN_WRITE)
#define TCP_INSPECT UINT64_C(1)
#define TCP_ABORT UINT64_C(2)
#define TCP_READ UINT64_C(3)
#define TCP_WRITE UINT64_C(4)
#define TCP_SHUTDOWN_WRITE UINT64_C(5)
#define TCP_READ_MAX_BYTES UINT64_C(4096)
#define TCP_READ_MAX_WAIT_NS UINT64_C(30000000000)
#define TCP_WRITE_MAX_BYTES UINT64_C(4096)
#define TCP_WRITE_MAX_WAIT_NS UINT64_C(30000000000)

#define TCP_STATE_CONNECTED UINT32_C(1)
#define TCP_STATE_PEER_CLOSED UINT32_C(2)
#define TCP_STATE_CLOSED UINT32_C(3)

#define TCP_INFO_WRITE_SHUTDOWN (UINT32_C(1) << 0)
#define TCP_INFO_PEER_FIN (UINT32_C(1) << 1)

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
  uint32_t flags; /* Local write shutdown and peer FIN are independent. */
};

struct tcp_connect_reply {
  handle_t handle;
  struct tcp_connection_info connection;
};

/* LISTEN binds exactly the configured local IPv4 address and a nonzero port.
 * Wildcard/ephemeral binding and address reuse are unsupported. Init creates
 * the listener and delegates it; CONNECT grants do not authorize LISTEN.
 * Four listeners globally, each with four combined half-open/ready connections.
 * All records also consume the shared 32-record transport budget. */
struct tcp_listen_request {
  struct message_header header;
  uint32_t address;
  uint16_t port;
  uint16_t reserved;
};

struct tcp_listener_info {
  uint32_t local_address;
  uint16_t local_port;
  uint16_t reserved;
  uint32_t pending; /* Includes handshakes and established, unaccepted streams. */
  uint32_t capacity;
  uint32_t terminal_status; /* CALL_OK while admitting; latched failure otherwise. */
  uint32_t reserved2;
};

struct tcp_listen_reply {
  handle_t handle;
  struct tcp_listener_info listener;
};

/* PROTOCOL_TCP_LISTENER: header-only INSPECT; ACCEPT returns an ordinary owned
 * stream and its endpoints after handshake. One outstanding accept per shared
 * listener (otherwise BUSY), including completed replies until collected.
 * The absolute monotonic deadline is at most 30 seconds ahead. Timeout leaves
 * the queue untouched; failure publishes no handle and leaves outputs untouched.
 * Copies share the listener. Final release stops admission and aborts pending
 * connections, while already accepted streams remain independent. Address loss
 * invalidates the listener permanently. No idle timeout for ready connections. */
struct tcp_accept_request {
  struct message_header header;
  uint64_t deadline_ns;
};

struct tcp_accept_reply {
  handle_t handle;
  struct tcp_connection_info connection;
};

/* PROTOCOL_TCP: header-only INSPECT/ABORT/SHUTDOWN_WRITE, with separate rights.
 * INSPECT returns tcp_connection_info even after failure. CLOSED describes the
 * transport (including TIME_WAIT); READ can still drain buffered bytes. PEER_FIN
 * does not mean EOF until those bytes are consumed. No raw TCP state is exposed.
 *
 * SHUTDOWN_WRITE commits shared write shutdown and schedules FIN after accepted
 * bytes; it does not wait for FIN allocation, transmission or acknowledgment.
 * Reads stay usable. Unaccepted/future nonempty writes fail ENDPOINT_CLOSED.
 * Repeating a committed shutdown succeeds, even after later failure; it does not
 * clear terminal_status. A first shutdown after terminal failure reports it.
 *
 * ABORT is idempotent, affects copies, discards unread bytes and preserves the
 * first failure. Final close without shutdown, or with unread data, also aborts.
 * Otherwise final close permits bounded graceful teardown. TIME_WAIT survives
 * abort/close until normal expiry. Closing one copy does not close other copies.
 * Neither shutdown nor close proves delivery. Both header-only mutators return
 * no reply bytes. */

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
 * further writes until local shutdown. Final close is not a flush. */
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
_Static_assert(sizeof(struct tcp_listen_request) == 24, "TCP listen request");
_Static_assert(sizeof(struct tcp_listener_info) == 24, "TCP listener info");
_Static_assert(sizeof(struct tcp_listen_reply) == 32, "TCP listen reply");
_Static_assert(sizeof(struct tcp_accept_request) == 24, "TCP accept request");
_Static_assert(sizeof(struct tcp_accept_reply) == 32, "TCP accept reply");

#endif
