#ifndef ABI_NET_FACTORY_H
#define ABI_NET_FACTORY_H

#include <abi/handle.h>
#include <abi/message.h>

/* Optional native path restrictions; zero fields permit scoped routing. */
struct net_path {
  uint64_t interface_id;
  uint32_t source;
  uint32_t reserved;
};

_Static_assert(sizeof(struct net_path) == 16, "network path restriction");

/* Each factory uses its existing protocol and operation rights. Enumeration
 * requires any held operation right and reveals only the immutable held scope,
 * in ascending boot-local ID order. A zero reply ID marks the end. */
struct net_factory_next_interface_request {
  struct message_header header;
  uint64_t after_id;
};

struct net_factory_next_interface_reply {
  uint64_t interface_id;
};

/* Derivation returns a distinct one-interface factory with a nonzero subset of
 * held operation rights and zero IPC transport rights. An out-of-scope ID or
 * invalid rights subset is DENIED without consulting global inventory.
 * Copies retain scope; derived grants retain the source's forwarder lineage. */
struct net_factory_derive_request {
  struct message_header header;
  uint64_t interface_id;
  uint64_t rights;
};

struct net_factory_derive_reply {
  handle_t handle;
};

_Static_assert(sizeof(struct net_factory_next_interface_request) == 24,
    "network factory scope cursor");
_Static_assert(sizeof(struct net_factory_next_interface_reply) == 8,
    "network factory scope identity");
_Static_assert(sizeof(struct net_factory_derive_request) == 32,
    "network factory derivation request");
_Static_assert(sizeof(struct net_factory_derive_reply) == 8,
    "network factory derivation reply");

#endif
