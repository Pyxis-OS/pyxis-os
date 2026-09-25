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
#define TCP_RIGHTS (TCP_RIGHT_INSPECT | TCP_RIGHT_ABORT)
#define TCP_INSPECT UINT64_C(1)
#define TCP_ABORT UINT64_C(2)

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
 * No READ/WRITE or write-shutdown operations are exposed yet. */
_Static_assert(sizeof(struct tcp_connect_request) == 32, "TCP connect request");
_Static_assert(sizeof(struct tcp_connection_info) == 24, "TCP connection info");
_Static_assert(sizeof(struct tcp_connect_reply) == 32, "TCP connect reply");

#endif
