#ifndef ABI_DESCRIBE_H
#define ABI_DESCRIBE_H

#include <abi/handle.h>
#include <abi/message.h>

#define RESOURCE_DETAIL_MAX_BYTES 255
#define RESOURCE_MEDIA_TYPE_MAX_BYTES 127
#define RESOURCE_ERROR_BODY_MAX_BYTES 65536

enum resource_outcome {
  RESOURCE_OK,
  RESOURCE_NOT_FOUND,
  RESOURCE_DENIED,
  RESOURCE_IO_ERROR,
  RESOURCE_TOO_LARGE,
  RESOURCE_ABORTED,
  RESOURCE_UNCERTAIN,
  RESOURCE_OUTCOME_COUNT,
};

#define RESOURCE_LENGTH_KNOWN (UINT64_C(1) << 0)
#define RESOURCE_REREADABLE (UINT64_C(1) << 1)
#define RESOURCE_COMPLETE (UINT64_C(1) << 2)
#define RESOURCE_ERROR_BODY (UINT64_C(1) << 3)
#define RESOURCE_FLAGS (RESOURCE_LENGTH_KNOWN | RESOURCE_REREADABLE | \
  RESOURCE_COMPLETE | RESOURCE_ERROR_BODY)

/* Declared fields confer no authority or proof. Strings are counted UTF-8 with
 * zeroed unused storage, no embedded NUL, and a trailing NUL. Unknown length is
 * zero with LENGTH_KNOWN clear. ERROR_BODY includes a zero-byte snapshot. */
struct resource_description {
  uint64_t outcome;
  uint64_t flags;
  uint64_t length;
  uint64_t detail_size;
  uint64_t media_type_size;
  char detail[RESOURCE_DETAIL_MAX_BYTES + 1];
  char media_type[RESOURCE_MEDIA_TYPE_MAX_BYTES + 1];
};

#define DESCRIBE_INCLUDE_ERROR_BODY UINT64_C(1)
#define DESCRIBE_IMMUTABLE_SNAPSHOT UINT64_C(1)

/* Protocol, kind, both masks and properties are attested by the kernel. A query
 * never calls a provider or waits for I/O/allocation. Including an error body
 * needs one immediately available local handle slot; otherwise CALL_LIMIT.
 * The READ-only native immutable body is owned independently of the provider,
 * including its exit and revocation. Closing it releases copied bytes only. */
struct describe_reply {
  uint64_t protocol;
  uint64_t kind;
  uint64_t rights;
  uint64_t transport;
  uint64_t properties;
  handle_t error_body;
  struct resource_description description;
};

#define ENDPOINT_DESCRIBE_PUBLISH UINT64_C(5)

/* Invoked on the issuer-owned receiver with CONTROL. The body bytes are copied
 * once into an immutable native FILE, not supplied as a grant. Publication is
 * atomic: failure preserves the prior snapshot. Clearing ERROR_BODY releases
 * the issuer's old body; previously returned bodies remain readable. */
struct endpoint_describe_publish {
  struct message_header header;
  uint64_t object_id;
  struct resource_description description;
  uint64_t error_body_address;
  uint64_t error_body_size;
};

_Static_assert(sizeof(struct resource_description) == 424, "description layout");
_Static_assert(sizeof(struct describe_reply) == 472, "describe reply layout");
_Static_assert(sizeof(struct endpoint_describe_publish) == 464, "description publication layout");

#endif
