#ifndef ABI_FORWARDER_H
#define ABI_FORWARDER_H

#include <abi/handle.h>
#include <abi/message.h>

#define FORWARDER_REVOCABLE UINT64_C(1)
#define FORWARDER_SLOT UINT64_C(2)
#define FORWARDER_CREATE_SAME_RIGHTS UINT64_C(1)
#define FORWARDER_RIGHT_CUT (UINT64_C(1) << 0)
#define FORWARDER_RIGHT_RETARGET (UINT64_C(1) << 1)
#define FORWARDER_RIGHTS (FORWARDER_RIGHT_CUT | FORWARDER_RIGHT_RETARGET)
#define FORWARDER_CUT UINT64_C(1)
#define FORWARDER_RETARGET UINT64_C(2)

/* Creation delegates only held authority; it returns both handles or neither.
 * Holders preserve the target interface. Controllers never imply target rights.
 * SAME_RIGHTS requires both masks zero, as for ordinary COPY. */
struct forwarder_create {
  handle_t target;
  uint64_t kind;
  uint64_t rights;
  uint64_t transport;
  uint64_t flags;
};

struct forwarder_reply {
  handle_t holder;
  handle_t controller;
};

/* An invalid target empties a service slot without changing its fixed type or
 * ceiling. Replacement needs the same attested protocol and representation
 * (native or exported); crossing representations returns WRONG_TYPE.
 * CUT is permanent. RETARGET is unavailable on a revocable copy. */
struct forwarder_retarget {
  struct message_header header;
  handle_t target;
};

_Static_assert(sizeof(struct forwarder_create) == 40, "forwarder creation layout");
_Static_assert(sizeof(struct forwarder_reply) == 16, "forwarder reply layout");

#endif
