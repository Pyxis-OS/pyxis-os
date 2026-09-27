#ifndef ABI_HANDLE_H
#define ABI_HANDLE_H

#include <stdint.h>

/* Opaque process-local capability. Never decode or use as an object identity. */
typedef uint64_t handle_t;
#define HANDLE_INVALID UINT64_C(0)

/* Resource rights and IPC transport authority attenuate independently.
 * Native kernel objects and receipts have zero transport rights. Raw endpoint
 * clients have zero resource rights. Owned receivers keep control rights separate
 * from receive authority. Exported clients carry both sets. */
#define HANDLE_TRANSPORT_SEND (UINT64_C(1) << 0)
#define HANDLE_TRANSPORT_RECEIVE (UINT64_C(1) << 1)
#define HANDLE_TRANSPORT_CALL (HANDLE_TRANSPORT_SEND | HANDLE_TRANSPORT_RECEIVE)

struct handle_authority {
  uint64_t rights;
  uint64_t transport;
};

/* COPY with this flag preserves both sets and requires rights == transport == 0.
 * Without it, each mask requests an exact subset of the source grant. */
#define HANDLE_COPY_SAME_RIGHTS UINT64_C(1)

#endif
