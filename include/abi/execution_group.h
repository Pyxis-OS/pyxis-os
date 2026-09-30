#ifndef ABI_EXECUTION_GROUP_H
#define ABI_EXECUTION_GROUP_H

#include <abi/message.h>

#define EXECUTION_GROUP_RIGHT_CONTROL (UINT64_C(1) << 0)
#define EXECUTION_GROUP_RIGHT_WAIT (UINT64_C(1) << 1)
#define EXECUTION_GROUP_SEAL UINT64_C(1)
#define EXECUTION_GROUP_TERMINATE UINT64_C(2)
#define EXECUTION_GROUP_WAIT UINT64_C(3)

/* SEAL sends only a message_header and returns no payload. CONTROL grants
 * retain supervision; losing the last such grant requests termination. Queued
 * IPC grants count, storage references and bound launchers do not. Sealing is
 * permanent and idempotent. It prevents publication of new children, including
 * an entire prepared batch, but does not stop existing members or wait for
 * their cleanup. TERMINATE requires CONTROL, seals admission and requests
 * stopping existing members; success acknowledges the request, not completion.
 * WAIT requires only WAIT and returns no payload after completion. All three
 * requests are header-only; sealing/termination are permanent and idempotent.
 * Completion is immutable: sealed admission, no members or admitted launches,
 * and no outstanding cleanup caused by their resource releases. It is not an
 * aggregate program success result. An open empty group is not complete.
 * WAIT grants/observations do not retain controlling supervision. */

#endif
