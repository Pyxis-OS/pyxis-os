#ifndef ABI_RETENTION_H
#define ABI_RETENTION_H

#include <abi/describe.h>
#include <abi/output.h>

/* Private trusted startup authority. Session configuration is the policy owner;
 * successors receive only the resulting immutable-limits account. */
#define RETENTION_FACTORY_RIGHT_CREATE (UINT64_C(1) << 0)
#define RETENTION_CREATE_ACCOUNT UINT64_C(1)
#define RETENTION_RIGHT_CAPTURE (UINT64_C(1) << 0)
#define RETENTION_CREATE_OUTPUT UINT64_C(1)
#define RETENTION_CREATE_FAILURE UINT64_C(2)
#define RETENTION_STATUS UINT64_C(3)

struct retention_limits {
  uint64_t value_limit;
  uint64_t session_limit;
};

struct retention_account_reply {
  handle_t account;
};

/* Charged logical FILE bytes, including holes, excluding allocation rounding.
 * Limits are positive, immutable and chosen by trusted session configuration.
 * The account's originating space is fixed at creation; copies share it.
 * There is no reset, increase, reassignment or explicit debit operation. */
struct retention_status_reply {
  uint64_t value_limit;
  uint64_t session_limit;
  uint64_t retained_bytes;
};

/* Followed by detail_size UTF-8 bytes, no NUL, at most RESOURCE_DETAIL_MAX_BYTES.
 * Only non-OK resource outcomes are accepted. Returns a sealed READ-only native
 * FILE: DESCRIBE preserves failure/detail, every data operation fails, no prefix.
 * The logical length and retention charge are zero. No error-body attachment. */
struct retention_failure_request {
  uint64_t outcome;
  uint64_t detail_size;
};

struct retention_failure_reply {
  handle_t value;
};

_Static_assert(sizeof(struct retention_limits) == 16, "retention limits");
_Static_assert(sizeof(struct retention_account_reply) == 8, "retention account");
_Static_assert(sizeof(struct retention_status_reply) == 24, "retention status");
_Static_assert(sizeof(struct retention_failure_request) == 16, "retention failure request");
_Static_assert(sizeof(struct retention_failure_reply) == 8, "retention failure reply");

#endif
