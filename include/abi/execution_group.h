#ifndef ABI_EXECUTION_GROUP_H
#define ABI_EXECUTION_GROUP_H

#include <abi/message.h>

#define EXECUTION_GROUP_RIGHT_CONTROL (UINT64_C(1) << 0)
#define EXECUTION_GROUP_SEAL UINT64_C(1)

/* SEAL sends only a message_header and returns no payload. CONTROL grants
 * retain supervision; losing the last such grant also seals admission. Queued
 * IPC grants count, storage references and bound launchers do not. Sealing is
 * permanent and idempotent. It prevents publication of new children, including
 * an entire prepared batch, but does not stop existing members or wait for
 * their cleanup. There is no termination or completion operation yet. */

#endif
