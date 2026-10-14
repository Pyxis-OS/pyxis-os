#ifndef ABI_PROVIDER_H
#define ABI_PROVIDER_H

#include <abi/endpoint.h>
#include <abi/file.h>
#include <abi/describe.h>

#define PROVIDER_OPEN UINT64_C(1)
#define PROVIDER_PREPARE UINT64_C(2)
#define PROVIDER_RIGHT_OPEN_READ (UINT64_C(1) << 0)
#define PROVIDER_RIGHT_OPEN_WRITE (UINT64_C(1) << 1)
#define PROVIDER_RIGHTS (PROVIDER_RIGHT_OPEN_READ | PROVIDER_RIGHT_OPEN_WRITE)
#define PROVIDER_INTENT_READ (UINT64_C(1) << 0)
#define PROVIDER_INTENT_WRITE (UINT64_C(1) << 1)
#define PROVIDER_INTENTS (PROVIDER_INTENT_READ | PROVIDER_INTENT_WRITE)
#define PROVIDER_OPEN_CREATE (UINT64_C(1) << 0)
#define PROVIDER_OPEN_EXCLUSIVE (UINT64_C(1) << 1)
#define PROVIDER_OPEN_TRUNCATE (UINT64_C(1) << 2)
#define PROVIDER_OPEN_APPEND (UINT64_C(1) << 3)
#define PROVIDER_OPEN_FLAGS (PROVIDER_OPEN_CREATE | PROVIDER_OPEN_EXCLUSIVE | \
  PROVIDER_OPEN_TRUNCATE | PROVIDER_OPEN_APPEND)

/* Followed by bounded opaque target bytes without NUL. The caller captures its
 * environment entry before OPEN; the provider interprets only the remainder.
 * The copied absolute deadline is also the endpoint delivery deadline.
 * EXCLUSIVE requires CREATE; TRUNCATE and APPEND require WRITE intent.
 * CREATE requires the service's OPEN_WRITE even with READ-only intent. */
struct provider_open_request {
  uint64_t intent;
  uint64_t flags;
  uint64_t target_size;
  uint64_t deadline_ns;
};

#define PROVIDER_TARGET_MAX_BYTES (ENDPOINT_DATA_MAX - sizeof(struct provider_open_request))

/* A typed resource reply transfers one owned resource when protocol is nonzero,
 * otherwise none. The kernel attests its actual protocol and masks. Outcome is
 * independent of protocol: failure is not empty data. Error bodies are obtained
 * from the resource's published native snapshot via DESCRIBE, never attached
 * provider-served capabilities. Local/admission failure carries no typed reply.
 * Descriptions contain no provider identity or protocol-specific status. */
struct provider_open_reply {
  uint64_t protocol;
  struct resource_description description;
};

_Static_assert(sizeof(struct provider_open_request) == 32, "provider open request layout");
_Static_assert(sizeof(struct provider_open_reply) == 432, "provider open reply layout");
_Static_assert(FILE_PAYLOAD_MAX <= ENDPOINT_DATA_MAX, "file payload fits endpoint delivery");

#endif
