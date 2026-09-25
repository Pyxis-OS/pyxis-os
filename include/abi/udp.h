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
 * SEND/RECEIVE rights are delegable, but their operations are not implemented
 * yet. Errors leave reply storage untouched. */
_Static_assert(sizeof(struct udp_open_request) == 24, "UDP open request");
_Static_assert(sizeof(struct udp_endpoint_info) == 16, "UDP endpoint info");
_Static_assert(sizeof(struct udp_open_reply) == 24, "UDP open reply");

#endif
