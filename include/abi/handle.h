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

#define HANDLE_KIND_NATIVE UINT64_C(1)
#define HANDLE_KIND_EXPORTED UINT64_C(2)

/* Restrictions limit what a grant may observe, independently of its rights.
 * The kernel adds them; every copy, transfer and derived grant keeps them, and
 * nothing removes them. DETAIL makes DESCRIBE omit the free-text detail. */
#define HANDLE_RESTRICT_DETAIL (UINT64_C(1) << 0)
#define HANDLE_RESTRICTIONS HANDLE_RESTRICT_DETAIL

/* Describes this grant, not object identity or current availability. Export
 * protocol/kind remain queryable after withdrawal or provider exit. A forwarder
 * grant reports its own restrictions; DESCRIBE also applies its target's. */
struct handle_info {
  uint64_t rights;
  uint64_t transport;
  uint64_t protocol;
  uint64_t kind;
  uint64_t restrictions;
};

_Static_assert(sizeof(struct handle_info) == 40, "handle info layout");

/* COPY with this flag preserves both sets and requires rights == transport == 0.
 * Without it, each mask requests an exact subset of the source grant. Either
 * way the copy keeps the source's restrictions. */
#define HANDLE_COPY_SAME_RIGHTS UINT64_C(1)

#endif
