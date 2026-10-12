#ifndef ABI_HTTP_TRANSACTION_H
#define ABI_HTTP_TRANSACTION_H

#include <abi/provider.h>
#include <abi/stream.h>

#define HTTP_TRANSACTION_COMMIT UINT64_C(1)
#define HTTP_TRANSACTION_ABORT UINT64_C(2)
#define HTTP_TRANSACTION_STATUS UINT64_C(3)
#define HTTP_TRANSACTION_RIGHT_COMMIT (UINT64_C(1) << 0)
#define HTTP_TRANSACTION_RIGHT_ABORT (UINT64_C(1) << 1)
#define HTTP_TRANSACTION_RIGHT_STATUS (UINT64_C(1) << 2)
#define HTTP_TRANSACTION_CONTROL_RIGHTS \
  (HTTP_TRANSACTION_RIGHT_COMMIT | HTTP_TRANSACTION_RIGHT_ABORT)

#define HTTP_PREPARE_GET UINT64_C(0)
#define HTTP_PREPARE_POST UINT64_C(1)
#define HTTP_PREPARE_CREDENTIALS (UINT64_C(1) << 0)
#define HTTP_PREPARE_JSON (UINT64_C(1) << 1)
#define HTTP_PREPARE_FLAGS (HTTP_PREPARE_CREDENTIALS | HTTP_PREPARE_JSON)

/* PROVIDER_PREPARE: URI then header lines, both copied without NUL. Header
 * lines are "name: value\r\n" with no terminating empty line. GET needs
 * OPEN_READ, POST needs separately delegated OPEN_WRITE. The original absolute
 * deadline includes preparation/staging and may exceed one native wait bound.
 * Credential-bearing fields stay in this private call, never startup or argv. */
struct http_prepare_request {
  uint64_t method, flags, deadline_ns, uri_size, headers_size;
};

/* PREPARE success transfers control, optional body, observation in that order.
 * These are separate exports. Observation does not retain control lifetime. */
struct http_prepare_reply {
  uint64_t method;
};

#define HTTP_COMMIT_RESPONSE (UINT64_C(1) << 0)
/* The first successful COMMIT reply transfers reader/observation after headers.
 * Later commits observe only, without another reader or a second submission. */
struct http_commit_reply {
  uint64_t flags, http_status;
};

enum http_transaction_phase {
  HTTP_TRANSACTION_PREPARING,
  HTTP_TRANSACTION_STAGING,
  HTTP_TRANSACTION_OPENING,
  HTTP_TRANSACTION_READING,
  HTTP_TRANSACTION_COMPLETE,
  HTTP_TRANSACTION_FAILED,
  HTTP_TRANSACTION_ABORTED,
};

enum http_submission_state {
  HTTP_SUBMISSION_NOT_STARTED,
  HTTP_SUBMISSION_ATTEMPTED,
  HTTP_SUBMISSION_SENT,
};

enum http_framing_state {
  HTTP_FRAMING_PENDING,
  HTTP_FRAMING_COMPLETE,
  HTTP_FRAMING_FAILED,
};

/* STATUS is observation-only. Native failure, HTTP status, framing, known
 * counters and cleanup diagnostics are separate. A missing final observation
 * never establishes completion. No credentials/request headers are returned. */
struct http_transaction_status {
  uint64_t phase, submission, http_status, framing;
  uint64_t status, source_status, cleanup_status;
  uint64_t uploaded, received, consumed, deadline_ns;
  uint64_t tls_error, tls_native_status;
  uint64_t tls_cleanup_attempted;
  char media_type[PROVIDER_MEDIA_TYPE_MAX_BYTES + 1];
  char location[PROVIDER_HTTP_URI_MAX_BYTES + 1];
};

_Static_assert(sizeof(struct http_prepare_request) == 40, "HTTP prepare layout");
_Static_assert(sizeof(struct http_transaction_status) == 2296, "HTTP status layout");

#endif
