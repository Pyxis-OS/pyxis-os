#ifndef ABI_PROVIDER_HTTP_PRIVATE_H
#define ABI_PROVIDER_HTTP_PRIVATE_H

#include <abi/provider.h>

/* Private continuation bridge until capability-model task B6. Generic OPEN
 * never falls back to this operation; PREPARE remains a separate HTTP path. */
#define PROVIDER_LEGACY_HTTP_OPEN UINT64_C(3)
#define PROVIDER_HTTP_URI_MAX_BYTES 2048
#define PROVIDER_HTTP_HOST_MAX_BYTES 255
#define PROVIDER_LEGACY_HTTP_CONTEXT (UINT64_C(1) << 0)
#define PROVIDER_LEGACY_HTTP_ORIGIN_CROSSED (UINT64_C(1) << 1)
#define PROVIDER_LEGACY_HTTP_FLAGS \
  (PROVIDER_LEGACY_HTTP_CONTEXT | PROVIDER_LEGACY_HTTP_ORIGIN_CROSSED)
#define PROVIDER_HTTP UINT64_C(0)
#define PROVIDER_HTTPS UINT64_C(1)
#define PROVIDER_LEGACY_HTTP_OUTCOME_ERROR UINT64_C(0)
#define PROVIDER_LEGACY_HTTP_OUTCOME_BYTES UINT64_C(1)
#define PROVIDER_LEGACY_HTTP_OUTCOME_REDIRECT UINT64_C(2)

/* Parsed initial origin, not destination authority. host is lowercase, with
 * no terminal DNS dot; port is the effective numeric port. */
struct provider_http_origin {
  uint64_t scheme;
  uint64_t port;
  char host[PROVIDER_HTTP_HOST_MAX_BYTES + 1];
};

struct provider_http_budget {
  uint64_t body_bytes;
  uint64_t header_bytes;
  uint64_t fields;
  uint64_t informational;
};

/* Existing continuation bytes, followed by the complete URI without NUL.
 * Context accounts for the chain; it confers no destination authority.
 * ORIGIN_CROSSED is sticky. Requested FILE access requires service OPEN rights. */
struct provider_legacy_http_open_request {
  uint64_t rights;
  uint64_t uri_size;
  uint64_t flags;
  struct provider_http_origin origin;
  struct provider_http_budget remaining;
};

#define PROVIDER_LEGACY_HTTP_TARGET_MAX_BYTES \
  (ENDPOINT_DATA_MAX - sizeof(struct provider_legacy_http_open_request))

/* Every reply carries this prefix. BYTES owns exactly one exported FILE with
 * the requested rights and CALL transport, followed by optional printable
 * media type. REDIRECT carries no grants/protocol/representation/media type,
 * followed by one Location URI reference. ERROR has no grants or strings.
 * provider_status is diagnostic (HTTP status), never authority. HTTP charges
 * describe decoded final body or discarded redirect body read-ahead, headers,
 * fields and informational responses. No transport error permits replay. */
struct provider_legacy_http_open_reply {
  uint64_t protocol;
  uint64_t representation;
  uint64_t media_type_size;
  uint64_t provider_status;
  uint64_t outcome;
  uint64_t location_size;
  struct provider_http_budget consumed;
};

_Static_assert(sizeof(struct provider_legacy_http_open_request) == 328,
  "private HTTP open request layout");
_Static_assert(sizeof(struct provider_legacy_http_open_reply) == 80,
  "private HTTP open reply layout");
_Static_assert(PROVIDER_HTTP_URI_MAX_BYTES <= PROVIDER_LEGACY_HTTP_TARGET_MAX_BYTES,
  "private HTTP URI fits continuation request");

#endif
