#ifndef ABI_RANDOM_H
#define ABI_RANDOM_H

#include <abi/message.h>

#define RANDOM_RIGHT_READ (UINT64_C(1) << 0)
#define RANDOM_READ UINT64_C(1)
#define RANDOM_MAX_BYTES UINT64_C(256)
#define RANDOM_MAX_WAIT_NS UINT64_C(5000000000)

/* PROTOCOL_RANDOM, READ right. Reply is exactly length random bytes on success;
 * every failure leaves output untouched. Zero length is a no-op after checking
 * the deadline and authority, even without a device. Larger than MAX is LIMIT.
 * Deadline is absolute monotonic time, at most MAX_WAIT_NS ahead. Past deadlines
 * time out. No fallback or partial success; host-supplied randomness is trusted.
 * Eight shared call slots include queued, active and unconsumed completions. */
struct random_read_request {
  struct message_header header;
  uint64_t length;
  uint64_t deadline_ns;
};

_Static_assert(sizeof(struct random_read_request) == 32, "random read request");

#endif
