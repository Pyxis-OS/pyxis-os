#ifndef ABI_PROVIDER_H
#define ABI_PROVIDER_H

#include <abi/endpoint.h>
#include <abi/file.h>

#define PROVIDER_OPEN UINT64_C(1)
#define PROVIDER_RIGHT_OPEN_READ (UINT64_C(1) << 0)
#define PROVIDER_RIGHT_OPEN_WRITE (UINT64_C(1) << 1)
#define PROVIDER_RIGHTS (PROVIDER_RIGHT_OPEN_READ | PROVIDER_RIGHT_OPEN_WRITE)
#define PROVIDER_REPRESENTATION_BYTES UINT64_C(1)
#define PROVIDER_MEDIA_TYPE_MAX_BYTES 127

/* Followed by the complete URI, without a terminator or embedded NUL. The
 * provider interprets it; clients do not normalize/decode its path. rights is a
 * nonzero subset of FILE_RIGHTS. Each requested access needs the corresponding
 * OPEN right on the service, independently of the returned file's authority.
 * Read-only providers reject writable opens before producing a resource. */
struct provider_open_request {
  uint64_t rights;
  uint64_t uri_size;
};

#define PROVIDER_URI_MAX_BYTES (ENDPOINT_DATA_MAX - sizeof(struct provider_open_request))

/* Successful OPEN carries exactly one owned exported FILE grant with exactly
 * the requested resource rights and CALL transport. The interface must agree
 * with HANDLE_INFO; this metadata is not authority. Only BYTES is implemented.
 * The prefix is followed by media_type_size printable ASCII bytes (no NUL),
 * or zero bytes when absent. A media type describes, but does not validate,
 * content. The immutable snapshot has stable SIZE and explicit-offset READ.
 * Application result uses call_status; failure carries no payload or grants.
 * Transport errors remain separate and must not trigger automatic retries. */
struct provider_open_reply {
  uint64_t protocol;
  uint64_t representation;
  uint64_t media_type_size;
};

_Static_assert(sizeof(struct provider_open_request) == 16, "provider open request layout");
_Static_assert(sizeof(struct provider_open_reply) == 24, "provider open reply layout");
_Static_assert(FILE_PAYLOAD_MAX <= ENDPOINT_DATA_MAX, "file payload fits endpoint delivery");

#endif
