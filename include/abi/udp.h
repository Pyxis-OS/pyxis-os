#ifndef ABI_UDP_H
#define ABI_UDP_H

#include <abi/handle.h>
#include <abi/message.h>

#define UDP_SERVICE_RIGHT_OPEN (UINT64_C(1) << 0)
#define UDP_OPEN UINT64_C(1)

#define UDP_RIGHT_INSPECT (UINT64_C(1) << 0)
#define UDP_RIGHT_SEND (UINT64_C(1) << 1)
#define UDP_RIGHT_RECEIVE (UINT64_C(1) << 2)
#define UDP_RIGHT_SHUTDOWN (UINT64_C(1) << 3)
#define UDP_RIGHTS (UDP_RIGHT_INSPECT | UDP_RIGHT_SEND | UDP_RIGHT_RECEIVE | UDP_RIGHT_SHUTDOWN)
#define UDP_INSPECT UINT64_C(1)
#define UDP_SHUTDOWN UINT64_C(2)
#define UDP_SEND UINT64_C(3)
#define UDP_RECEIVE UINT64_C(4)
#define UDP_MAX_PAYLOAD UINT64_C(1472)
#define UDP_SEND_MAX_WAIT_NS UINT64_C(5000000000)
#define UDP_RECEIVE_MAX_WAIT_NS UINT64_C(30000000000)

#define UDP_STATE_BOUND UINT32_C(1)
#define UDP_STATE_SHUTDOWN UINT32_C(2)
#define UDP_STATE_UNAVAILABLE UINT32_C(3)

/* OPEN uses PROTOCOL_UDP_SERVICE; address/port are host-order integers.
 * Bind an explicit 127/8 address or the assigned NIC address. Port zero selects
 * a free ephemeral port. No wildcard, reuse or privileged-port distinction.
 * Success returns a new endpoint with UDP_RIGHTS; copies share its lifetime. */
struct udp_open_request {
  struct message_header header;
  uint32_t address;
  uint16_t port;
  uint16_t reserved;
};

struct udp_endpoint_info {
  uint32_t address;
  uint16_t port;
  uint16_t reserved;
  uint32_t state;
  uint32_t reserved2;
};

struct udp_open_reply {
  handle_t handle;
  struct udp_endpoint_info local;
};

/* INSPECT and SHUTDOWN are header-only PROTOCOL_UDP requests with their own
 * rights. INSPECT returns udp_endpoint_info even after shutdown/invalidation.
 * SHUTDOWN returns no bytes, is idempotent and releases the binding before
 * success. Closing a handle only releases that reference; final cleanup is
 * deferred to the worker. Removed addresses never revive old endpoints.
 * Errors leave reply storage untouched. */
/* SEND requires SEND, copies one complete payload and returns no bytes.
 * Success means local-queue/NIC acceptance, not remote delivery. Destination
 * port must be nonzero; zero-length payloads are valid. Oversize is CALL_LIMIT.
 * Deadlines use CLOCK_NOW's monotonic epoch and include queue/ARP time. Past
 * deadlines time out even if work could otherwise complete immediately. */
struct udp_send_request {
  struct message_header header;
  uint32_t address;
  uint16_t port;
  uint16_t reserved;
  uint64_t buffer;
  uint64_t length;
  uint64_t deadline_ns;
};

/* RECEIVE requires RECEIVE and returns one datagram, never stream EOF.
 * The data buffer and reply must not overlap. At most UDP_MAX_PAYLOAD bytes of
 * buffer capacity are checked/touched. BUFFER_TOO_SMALL preserves the queued
 * head datagram; every failure leaves data and reply untouched. Retry with the
 * maximum capacity; errors do not return a required size. */
struct udp_receive_request {
  struct message_header header;
  uint64_t buffer;
  uint64_t capacity;
  uint64_t deadline_ns;
};

struct udp_receive_reply {
  uint32_t address;
  uint16_t port;
  uint16_t reserved;
  uint64_t length;
};

/* One outstanding call per direction per endpoint, including through copies:
 * another returns BUSY. Eight send and sixteen receive slots are independent of
 * control slots; exhaustion is QUEUE_FULL. Completion retains its slot until
 * the caller resumes. SHUTDOWN wakes pending I/O with ENDPOINT_CLOSED; address
 * removal wakes it with UNAVAILABLE and discards queued data. */
_Static_assert(sizeof(struct udp_send_request) == 48, "UDP send request");
_Static_assert(sizeof(struct udp_receive_request) == 40, "UDP receive request");
_Static_assert(sizeof(struct udp_receive_reply) == 16, "UDP receive reply");

_Static_assert(sizeof(struct udp_open_request) == 24, "UDP open request");
_Static_assert(sizeof(struct udp_endpoint_info) == 16, "UDP endpoint info");
_Static_assert(sizeof(struct udp_open_reply) == 24, "UDP open reply");

#endif
