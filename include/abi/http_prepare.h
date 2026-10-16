#ifndef ABI_HTTP_PREPARE_H
#define ABI_HTTP_PREPARE_H

#include <abi/provider.h>

/* HTTP-specific issuance on an existing PROVIDER root, not generic OPEN.
 * The operation selects no additional authority. Reply is provider_open_reply
 * with one PROVIDER grant attenuated from the invoking root, in its lineage.
 * Configured exports reject reconfiguration and nonempty OPEN/REMOVE tails. */
#define HTTP_PREPARE_ENDPOINT UINT64_C(4)
#define HTTP_ENDPOINT_GET UINT64_C(0)
#define HTTP_ENDPOINT_NAVIGATION (UINT64_C(1) << 0)
#define HTTP_ENDPOINT_RESPONSE_FLAGS HTTP_ENDPOINT_NAVIGATION
#define HTTP_ENDPOINT_POST UINT64_C(1)
#define HTTP_ENDPOINT_PUT UINT64_C(3)
#define HTTP_ENDPOINT_PATCH UINT64_C(4)

/* Followed by target_size opaque root-tail bytes then headers_size CRLF fields;
 * no NUL. Complete message fits ENDPOINT_DATA_MAX. timeout_ns is a per-use
 * duration; deadline_ns bounds this issuance call only. No network on issuance.
 * Supplied fields are private credentials and discarded on cross-origin hops. */
struct http_endpoint_request {
  uint64_t method;
  uint64_t response_flags;
  uint64_t timeout_ns;
  uint64_t deadline_ns;
  uint64_t target_size;
  uint64_t headers_size;
};

_Static_assert(sizeof(struct http_endpoint_request) == 48, "HTTP endpoint request layout");
#endif
