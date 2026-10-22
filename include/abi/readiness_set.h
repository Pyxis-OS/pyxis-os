#ifndef ABI_READINESS_SET_H
#define ABI_READINESS_SET_H

#include <abi/message.h>
#include <abi/wait.h>

#define READINESS_SET_RIGHT_CONTROL (UINT64_C(1) << 0)
#define READINESS_SET_RIGHT_WAIT (UINT64_C(1) << 1)
#define READINESS_SET_RIGHTS (READINESS_SET_RIGHT_CONTROL | READINESS_SET_RIGHT_WAIT)

#define READINESS_SET_REGISTER UINT64_C(1)
#define READINESS_SET_UPDATE UINT64_C(2)
#define READINESS_SET_REMOVE UINT64_C(3)
#define READINESS_SET_WAIT UINT64_C(4)
#define READINESS_SET_WAKE UINT64_C(5)
#define READINESS_SET_INFO UINT64_C(6)

/* Reply batching only, never a limit on registered or ready sources. */
#define READINESS_SET_BATCH_MAX UINT64_C(64)
#define READINESS_SET_WOKEN (UINT64_C(1) << 0)

struct readiness_set_create_reply {
  handle_t handle;
};

struct readiness_set_register_request {
  struct message_header header;
  handle_t target;
  uint64_t allowed_events;
  uint64_t active_events;
  uint64_t observed_generation;
  uint64_t tag;
};

struct readiness_set_registration {
  uint64_t identity;
  uint64_t generation;
};

struct readiness_set_update_request {
  struct message_header header;
  struct readiness_set_registration registration;
  uint64_t active_events;
  uint64_t observed_generation;
};

struct readiness_set_remove_request {
  struct message_header header;
  struct readiness_set_registration registration;
};

struct readiness_set_event {
  struct readiness_set_registration registration;
  uint64_t tag;
  uint64_t events;
  uint64_t status;
};

struct readiness_set_wait_request {
  struct message_header header;
  uint64_t deadline;
  uint64_t events_address;
  uint64_t capacity;
};

struct readiness_set_wait_reply {
  uint64_t count;
  uint64_t flags;
};

struct readiness_set_info {
  uint64_t budget_bytes;
  uint64_t charged_bytes;
  uint64_t registrations;
};

/* SYSCALL_READINESS_SET_CREATE takes reply address/capacity in RDI/RSI.
 * Creation is ungranted and returns owner-local CONTROL|WAIT, or no handle.
 * All sets share the process metadata budget. Operations require the creating
 * process, including through copies/forwarders. Last CONTROL closure shuts
 * down observation; WAIT-only handles can still observe closure.
 *
 * REGISTER captures source storage and independently revocable lineage without
 * prolonging logical source ownership. allowed_events is nonzero; active_events
 * is a subset, possibly zero; terminal source errors remain observable while
 * disarmed. UPDATE returns a new generation and rechecks level
 * state; it cannot expand the allowed mask. REMOVE joins producers before return.
 * Identities/generations never wrap. Stale keys are NOT_FOUND.
 *
 * WAIT collects only ready records, with at most BATCH_MAX per reply. Deadline
 * zero polls; absolute finite deadlines use WAIT_MAX_WAIT_NS. WOKEN is a sticky,
 * coalesced explicit wake. One collector; concurrent WAIT returns BUSY. Level
 * readiness reserves/consumes no source I/O. Buffered EOF and half-close preserve
 * the surviving directions. Terminal registration errors are delivered once,
 * with non-OK status and events zero.
 * A bad output buffer does not consume queued notification state.
 *
 * INFO reports process-wide charged metadata and this set's registration count.
 * Registration budget exhaustion returns LIMIT, allocation exhaustion NO_MEMORY,
 * before effects. Invalid operations, masks and sources are BAD_REQUEST. */

_Static_assert(sizeof(struct readiness_set_event) == 40, "readiness event layout");

#endif
