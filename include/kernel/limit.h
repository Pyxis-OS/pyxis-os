#ifndef KERNEL_LIMIT_H
#define KERNEL_LIMIT_H

#include <abi/limit.h>
#include <abi/syscall.h>

static inline enum call_status call_limit_set(struct call_limit *detail,
    uint64_t reason, uint64_t requested, uint64_t limit, uint64_t flags)
{
  if (detail) {
    *detail = (struct call_limit){reason, requested, limit, flags};
  }
  return CALL_LIMIT;
}

static inline enum call_status call_limit_encoding(struct call_limit *detail)
{
  return call_limit_set(detail, CALL_LIMIT_REQUEST_ENCODING, UINT64_MAX, UINT64_MAX,
      CALL_LIMIT_HAS_REQUESTED | CALL_LIMIT_HAS_LIMIT | CALL_LIMIT_REQUESTED_SATURATED);
}

static inline enum call_status call_limit_reference(struct call_limit *detail)
{
  return call_limit_set(detail, CALL_LIMIT_REFERENCE_COUNT, 0, 0, 0);
}

#endif
