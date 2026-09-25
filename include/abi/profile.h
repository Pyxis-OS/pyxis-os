#ifndef ABI_PROFILE_H
#define ABI_PROFILE_H

#include <abi/message.h>

#define PROFILE_RIGHT_MEMORY (UINT64_C(1) << 0)
#define PROFILE_BEGIN UINT64_C(1)
#define PROFILE_SNAPSHOT UINT64_C(2)
#define PROFILE_END UINT64_C(3)
#define PROFILE_ACTIVE (UINT64_C(1) << 0)
#define PROFILE_SATURATED (UINT64_C(1) << 1)

struct profile_duration {
  uint64_t total_ns;
  uint64_t maximum_ns;
};

struct profile_memory_operation {
  uint64_t requests;
  uint64_t failures;
  uint64_t requested_bytes; /* Page-rounded bytes, including failed requests. */
  uint64_t completed_bytes; /* Successful allocations or releases only. */
  struct profile_duration publication;
  struct profile_duration queue;
  struct profile_duration service;
  struct profile_duration resume;
  struct profile_duration total;
};

struct profile_snapshot {
  uint64_t flags;
  struct profile_memory_operation allocate;
  struct profile_memory_operation release;
};

/* All operations are header-only and require MEMORY authority. They always
 * act on the calling process, even through copied/delegated handles. One
 * collection per process, initially disabled; children start disabled too.
 * BEGIN clears counters, enables collection and has no reply (BUSY if active).
 * SNAPSHOT returns current/last counters without resetting or stopping them.
 * END disables collection and returns final counters (BAD_REQUEST if inactive).
 * Closing a handle does not stop collection; END or process exit does.
 *
 * Only requests admitted to the private-memory scheduler service are counted,
 * including failures there. Malformed/denied syscalls and other kernel memory
 * work are excluded. Times are elapsed monotonic ns, including instrumentation
 * and scheduling, NOT CPU time. Boundaries: request preparation, just before
 * publication locking, BSP service start/end, caller resumption. Queue time
 * therefore includes publication locking; total excludes syscall validation,
 * reply copying and counter accumulation. No per-request clock reads when off.
 * Totals saturate at UINT64_MAX and set SATURATED. The reply contains no
 * addresses. Bad reply storage must not change collection state. */
_Static_assert(sizeof(struct profile_duration) == 16, "profile duration layout");
_Static_assert(sizeof(struct profile_memory_operation) == 112, "profile operation layout");
_Static_assert(sizeof(struct profile_snapshot) == 232, "profile snapshot layout");

#endif
