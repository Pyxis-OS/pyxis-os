#ifndef ABI_UDP_H
#define ABI_UDP_H

#include <abi/handle.h>
#include <abi/message.h>
#include <abi/net_factory.h>

#define UDP_SERVICE_RIGHT_OPEN (UINT64_C(1) << 0)
#define UDP_SERVICE_RIGHT_BROADCAST (UINT64_C(1) << 1)
#define UDP_OPEN UINT64_C(1)
#define UDP_OPEN_ROUTE UINT64_C(2)
#define UDP_OPEN_BROADCAST UINT64_C(3)
#define UDP_SERVICE_NEXT_INTERFACE UINT64_C(4)
#define UDP_SERVICE_DERIVE UINT64_C(5)

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

/* OPEN binds path.source exactly; OPEN_ROUTE selects a source for address
 * within the held scope and path restrictions. Neither operation sends packets.
 * OPEN_BROADCAST requires source zero and one selected interface, either an
 * explicit allowed ID or a one-interface scope. Ambiguity is BAD_REQUEST before
 * port admission. Its address-independent endpoint survives address changes.
 * Binding/ephemeral-port conflicts and all admission quotas remain global. */
struct udp_open_request {
  struct message_header header;
  struct net_path path;
  uint16_t port;
  uint16_t reserved;
  uint32_t reserved2;
};

struct udp_open_route_request {
  struct message_header header;
  struct net_path path;
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
  uint64_t interface_id;
  uint64_t address_epoch;
};

struct udp_open_reply {
  handle_t handle;
  struct udp_endpoint_info local;
};

/* INSPECT and SHUTDOWN are header-only PROTOCOL_UDP requests with their own
 * rights. INSPECT returns udp_endpoint_info even after shutdown/invalidation.
 * SHUTDOWN returns no bytes, is idempotent and releases the binding before
 * success. Closing a handle only releases that reference; final cleanup is
 * deferred to the worker. Removed addresses never revive concrete endpoints.
 * Wildcard endpoints survive address replacement/clear, retaining received
 * datagrams and pending receives. ARP-pending sends still fail on replacement/clear.
 * Errors leave reply storage untouched. */
/* SEND requires SEND, copies one complete payload and returns no bytes.
 * Success means local-queue/NIC acceptance, not remote delivery. Destination
 * port must be nonzero; zero-length payloads are valid. Oversize is CALL_LIMIT.
 * Wildcard sends use the current the pinned interface address. Limited broadcast uses Ethernet
 * broadcast directly, with source zero only when unassigned. Other destinations
 * require an assigned address and ordinary the pinned interface unicast routing; no loopback.
 * Ordinary endpoints cannot send broadcast. Subnet-broadcast sending is unsupported.
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
 * another returns BUSY. Dynamic records and captured bytes consume the shared
 * TCP/UDP storage allowance; exhaustion refuses with NO_MEMORY. Completion
 * retains direction ownership until collection or native completion transfer. SHUTDOWN wakes pending I/O with ENDPOINT_CLOSED; address
 * removal wakes concrete endpoints with UNAVAILABLE and discards their queued data. */
_Static_assert(sizeof(struct udp_send_request) == 48, "UDP send request");
_Static_assert(sizeof(struct udp_receive_request) == 40, "UDP receive request");
_Static_assert(sizeof(struct udp_receive_reply) == 16, "UDP receive reply");

_Static_assert(sizeof(struct udp_open_route_request) == 40, "UDP routed open request");
_Static_assert(sizeof(struct udp_open_request) == 40, "UDP open request");
_Static_assert(sizeof(struct udp_endpoint_info) == 32, "UDP endpoint info");
_Static_assert(sizeof(struct udp_open_reply) == 40, "UDP open reply");

#endif
