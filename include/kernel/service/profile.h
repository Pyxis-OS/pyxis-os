#ifndef KERNEL_SERVICE_PROFILE_H
#define KERNEL_SERVICE_PROFILE_H

#include <abi/profile.h>
#include <abi/syscall.h>
#include <kernel/panic.h>

struct task_profile {
  struct profile_snapshot memory;
  struct profile_file_snapshot file;
  struct profile_host_snapshot host;
};

/* BSP, IF=0. Task creation provisions zeroed persistent caller aggregates;
 * retirement releases them after the caller can no longer use them. */
struct task_profile *profile_storage_create(void);
void profile_storage_destroy(struct task_profile *profile);

/* Current user task, IF=0. Caller-only storage; never lend it to a service. */
struct profile_snapshot *profile_memory_current(void);
struct profile_file_snapshot *profile_file_current(void);
struct profile_host_snapshot *profile_host_current(void);

/* Current user task, IF=0. Caller validates reply storage before state changes.
 * No allocation or remote inspection; one task per process at present. */
enum call_status profile_memory_control(uint64_t operation, struct profile_snapshot *reply);
enum call_status profile_file_control(uint64_t operation, struct profile_file_snapshot *reply);
enum call_status profile_host_control(uint64_t operation, struct profile_host_snapshot *reply);

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
