#ifndef ABI_ECHO_H
#define ABI_ECHO_H

#include <abi/message.h>

#define ECHO_RIGHT_SEND (UINT64_C(1) << 0)
#define ECHO_EXCHANGE UINT64_C(1)
#define ECHO_MAX_WAIT_NS UINT64_C(5000000000)
#define ECHO_PAYLOAD_BYTES 32

/* Addresses are host-order IPv4 integers (127.0.0.1 == 0x7f000001), not wire
 * headers. reserved must be zero. Deadline uses CLOCK_NOW's monotonic epoch;
 * at most ECHO_MAX_WAIT_NS into the future. A past deadline returns TIMED_OUT.
 * The kernel selects all wire identifiers and payload bytes. */
struct echo_request {
  struct message_header header;
  uint32_t destination;
  uint32_t reserved;
  uint64_t deadline_ns;
};

/* Successful matched reply. RTT includes deferred local processing, measured
 * from submission to transport, not time waiting for a request slot/worker.
 * Errors return zero bytes and leave reply storage untouched. */
struct echo_reply {
  uint64_t round_trip_ns;
  uint32_t address;
  uint16_t identifier;
  uint16_t sequence;
};

/* ECHO_RIGHT_SEND grants echo exchange only, not raw packets/configuration.
 * CALL_QUEUE_FULL bounds outstanding calls (including unconsumed completions)
 * to 16 system-wide. Closing another grant does not cancel this call. No task
 * cancellation or asynchronous submission is supplied by this protocol. */
_Static_assert(sizeof(struct echo_request) == 32, "echo request layout");
_Static_assert(sizeof(struct echo_reply) == 16, "echo reply layout");

#endif
