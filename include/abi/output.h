#ifndef ABI_OUTPUT_H
#define ABI_OUTPUT_H

#include <abi/file.h>
#include <abi/handle.h>
#include <abi/pipe.h>

/* Construction uses pipe CREATE authority. This slice supplies transport;
 * session retention accounting is the next prerequisite. */
#define PIPE_CREATE_OUTPUT UINT64_C(2)
#define OUTPUT_RIGHT_COLLECT (UINT64_C(1) << 0)
#define OUTPUT_STATUS UINT64_C(1)
#define OUTPUT_SEAL UINT64_C(2)
#define OUTPUT_TAKE UINT64_C(3)
#define OUTPUT_CANCEL UINT64_C(4)

#define OUTPUT_MODE_EMPTY UINT64_C(0)
#define OUTPUT_MODE_BYTES UINT64_C(1)
#define OUTPUT_MODE_VALUE UINT64_C(2)
#define OUTPUT_SEALED (UINT64_C(1) << 0)
#define OUTPUT_FAILED (UINT64_C(1) << 1)
#define OUTPUT_READY (UINT64_C(1) << 2)
#define OUTPUT_TAKEN (UINT64_C(1) << 3)

struct output_create_reply {
  handle_t collector;
  handle_t producer;
};

/* FILE_HANDOFF has no reply. Moves both explicitly owned handles together,
 * preserving wrappers and masks. Optional observation is HANDLE_INVALID.
 * Never pass borrowed libc descriptor handles. Source handles must be distinct.
 * BAD_OPERATION from an ordinary native FILE is unsupported/fallback safe.
 * Other failures do not invite byte fallback; uncertainty is never retried.
 * Success invalidates source generations without logical CLOSE effects.
 * Rejected transfers preserve their handles. */
struct output_handoff_request {
  handle_t value;
  handle_t observation;
};

struct output_status_reply {
  uint64_t mode;
  uint64_t flags;
};

/* Collector: creating process only, not copyable/delegatable/forwardable or
 * storable in environments/handoff payloads. Producer: native WRITE-only FILE.
 * First nonempty write or successful handoff selects mode; mixed output or a
 * second value latches failure and nothing successful is published. Zero writes
 * select neither. SEAL closes admission after separately observed job/subtree
 * retirement. STATUS/TAKE are nonblocking; TAKE is WOULD_BLOCK until sealed and
 * admitted writes/handoffs drain. Local slot/policy failure leaves the value.
 * Success moves ownership once. CANCEL/final logical collector close drains
 * queued ownership. wait_many accepts WAIT_COMPLETE and WAIT_CLOSED. */
struct output_take_reply {
  handle_t value;
  handle_t observation;
};

_Static_assert(sizeof(struct output_create_reply) == 16, "output create reply");
_Static_assert(sizeof(struct output_handoff_request) == 16, "output handoff request");
_Static_assert(sizeof(struct output_status_reply) == 16, "output status reply");
_Static_assert(sizeof(struct output_take_reply) == 16, "output take reply");

#endif
