#ifndef ABI_NAMESPACE_H
#define ABI_NAMESPACE_H

#include <abi/handle.h>
#include <abi/message.h>

#define NAMESPACE_SERVICE_RIGHT_CREATE (UINT64_C(1) << 0)
#define NAMESPACE_CREATE UINT64_C(1)
#define NAMESPACE_RIGHT_LOOKUP (UINT64_C(1) << 0)
#define NAMESPACE_RIGHT_MANAGE (UINT64_C(1) << 1)
#define NAMESPACE_RIGHTS (NAMESPACE_RIGHT_LOOKUP | NAMESPACE_RIGHT_MANAGE)
#define NAMESPACE_PUBLISH UINT64_C(1)
#define NAMESPACE_REPLACE UINT64_C(2)
#define NAMESPACE_REMOVE UINT64_C(3)
#define NAMESPACE_LOOKUP UINT64_C(4)
#define NAMESPACE_BINDINGS_MAX 64
#define NAMESPACE_NAME_MAX 63

/* Exact, case-sensitive names: ASCII letters, digits, '_', '-', '.', '+'.
 * name is NUL-terminated; the rest of its array must be zero. PUBLISH requires
 * absence, REPLACE requires presence. Both retain an exported client grant;
 * neither can increase either authority mask. REMOVE releases only the binding.
 * Namespace handles are ordinary copyable grants, with zero transport authority.
 * Lookup returns the binding's fixed authority. Withdrawn/dead exports report
 * ENDPOINT_CLOSED and remain bound until explicitly removed/replaced. */
struct namespace_name_message {
  struct message_header header;
  char name[NAMESPACE_NAME_MAX + 1];
};

struct namespace_bind_message {
  struct message_header header;
  char name[NAMESPACE_NAME_MAX + 1];
  handle_t client;
  uint64_t rights;
  uint64_t transport;
};

/* CREATE and LOOKUP each install one local handle. CREATE returns LOOKUP|MANAGE.
 * No receiver ownership or process lifetime is attached to a namespace. Its
 * final reference releases all bindings through BSP retirement. */
struct namespace_reply {
  handle_t handle;
};

_Static_assert(sizeof(struct namespace_name_message) == 80, "namespace name layout");
_Static_assert(sizeof(struct namespace_bind_message) == 104, "namespace bind layout");

#endif
