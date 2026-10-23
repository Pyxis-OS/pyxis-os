#ifndef ABI_EXECUTION_GROUP_H
#define ABI_EXECUTION_GROUP_H

#include <abi/message.h>

#define EXECUTION_GROUP_RIGHT_CONTROL (UINT64_C(1) << 0)
#define EXECUTION_GROUP_RIGHT_WAIT (UINT64_C(1) << 1)
#define EXECUTION_GROUP_SEAL UINT64_C(1)
#define EXECUTION_GROUP_TERMINATE UINT64_C(2)
#define EXECUTION_GROUP_WAIT UINT64_C(3)

/* Sticky subtree observations, immutable once completion is visible. */
#define EXECUTION_GROUP_USED_PASSTHROUGH (UINT64_C(1) << 0)
#define EXECUTION_GROUP_ABNORMAL (UINT64_C(1) << 1)
#define EXECUTION_GROUP_RESULT_FLAGS (EXECUTION_GROUP_USED_PASSTHROUGH | EXECUTION_GROUP_ABNORMAL)

struct execution_group_result {
  uint64_t flags;
};

/* SEAL sends only a message_header and returns no payload. CONTROL grants
 * retain supervision; losing the last such grant requests termination. Queued
 * IPC grants count, storage references and bound launchers do not. Sealing is
 * permanent and idempotent throughout the subtree. It prevents publication of
 * new members and subgroups, including
 * an entire prepared batch, but does not stop existing members or wait for
 * their cleanup. TERMINATE requires CONTROL, seals admission and requests
 * stopping existing members throughout the subtree; success acknowledges the request, not completion.
 * WAIT requires only WAIT. A zero reply capacity observes completion alone;
 * otherwise it returns execution_group_result. PASSTHROUGH includes retained
 * grants installed into members, even if released before exit. ABNORMAL means
 * termination was requested or a member faulted, was terminated or failed startup;
 * a normal nonzero exit is not abnormal. Observations include descendant groups.
 * All three
 * requests are header-only; sealing/termination are permanent and idempotent.
 * Completion is immutable: sealed admission, no members or admitted launch/group
 * preparations, every subgroup complete, and no outstanding attributed cleanup.
 * Retained completed-group storage does not delay parent completion. It is not an
 * aggregate program success result. An open empty group is not complete.
 * WAIT grants/observations do not retain controlling supervision. */

#endif
