#ifndef KERNEL_SERVICE_PROFILE_H
#define KERNEL_SERVICE_PROFILE_H

#include <abi/profile.h>
#include <kernel/panic.h>

static inline void profile_add(uint64_t *flags, uint64_t *total, uint64_t amount)
{
  if (amount > UINT64_MAX - *total) {
    *total = UINT64_MAX;
    *flags |= PROFILE_SATURATED;
  } else {
    *total += amount;
  }
}

static inline void profile_duration_add(uint64_t *flags, struct profile_duration *duration,
    uint64_t start, uint64_t end)
{
  KASSERT(end >= start);
  uint64_t elapsed = end - start;
  profile_add(flags, &duration->total_ns, elapsed);
  if (elapsed > duration->maximum_ns) {
    duration->maximum_ns = elapsed;
  }
}

#endif
