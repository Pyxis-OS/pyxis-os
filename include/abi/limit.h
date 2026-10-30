#ifndef ABI_LIMIT_H
#define ABI_LIMIT_H

#include <stdint.h>

/* The reason identifies both the quantity and its limiting source. Numeric
 * fields are meaningful only with their corresponding HAS flag. */
enum call_limit_reason {
  CALL_LIMIT_NONE = 0,
  CALL_LIMIT_REQUEST_ENCODING = 1,
  CALL_LIMIT_STARTUP_ADDRESS = 2,
  CALL_LIMIT_ENVIRONMENT_ENTRIES = 3,
  CALL_LIMIT_ENVIRONMENT_TEXT = 4,
  CALL_LIMIT_STACK_ADDRESS = 5,
  CALL_LIMIT_HANDLE_ENCODING = 6,
  CALL_LIMIT_REFERENCE_COUNT = 7,
  CALL_LIMIT_GROUP_MEMBERS = 8,
  CALL_LIMIT_READINESS_BYTES = 9,
  CALL_LIMIT_FORWARDER_LINEAGE = 10,
  CALL_LIMIT_GROUP_DEPTH = 11,
  CALL_LIMIT_GROUP_COUNT = 12,
  CALL_LIMIT_GROUP_LAUNCHES = 13,
  CALL_LIMIT_READINESS_IDENTITY = 14,
  CALL_LIMIT_READINESS_GENERATION = 15,
};

#define CALL_LIMIT_HAS_REQUESTED UINT64_C(1)
#define CALL_LIMIT_HAS_LIMIT UINT64_C(2)
#define CALL_LIMIT_REQUESTED_SATURATED UINT64_C(4)
#define CALL_LIMIT_FLAGS (CALL_LIMIT_HAS_REQUESTED | CALL_LIMIT_HAS_LIMIT | \
    CALL_LIMIT_REQUESTED_SATURATED)

struct call_limit {
  uint64_t reason;
  uint64_t requested;
  uint64_t limit;
  uint64_t flags;
};

_Static_assert(sizeof(struct call_limit) == 32, "call limit layout");

#endif
