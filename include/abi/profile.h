#ifndef ABI_PROFILE_H
#define ABI_PROFILE_H

#include <abi/message.h>

#define PROFILE_RIGHT_MEMORY (UINT64_C(1) << 0)
#define PROFILE_RIGHT_FILE (UINT64_C(1) << 1)
#define PROFILE_FILE_BEGIN UINT64_C(4)
#define PROFILE_FILE_SNAPSHOT UINT64_C(5)
#define PROFILE_FILE_END UINT64_C(6)
#define PROFILE_RIGHT_HOST (UINT64_C(1) << 2)
#define PROFILE_HOST_BEGIN UINT64_C(7)
#define PROFILE_HOST_SNAPSHOT UINT64_C(8)
#define PROFILE_HOST_END UINT64_C(9)
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
  struct profile_duration service;
  struct profile_duration total;
};

struct profile_snapshot {
  uint64_t flags;
  struct profile_memory_operation allocate;
  struct profile_memory_operation release;
};

struct profile_file_snapshot {
  uint64_t flags;
  uint64_t requests, successes, failures;
  uint64_t requested_capacity, copied_bytes;
  struct profile_duration publication, queue, service, resume, total;
  struct profile_duration allocation, copy, release;
};

/* FILE_BEGIN/SNAPSHOT/END require FILE authority and control an independent
 * caller-local collection with the same state rules as the memory operations.
 * Count each RAM buffer replacement admitted to BSP, including the existing
 * exact-capacity fallback after a failed spare-capacity allocation. Capacity
 * sums include failures and zero-capacity releases; copied bytes count only
 * existing file contents actually copied, never incoming write payloads.
 * Boundaries: preparation before wait setup, just before publication locking,
 * BSP service start/end and caller resumption. Service includes allocation,
 * existing-data copy and old-buffer release, separately timed when performed.
 * Zero-capacity requests only release; failed allocation does not copy/release.
 * No clocks or allocations for disabled collection. Elapsed ns include clock
 * overhead and scheduling, not CPU time. Saturation is local to this collection.
 * File ownership waits, payload copying, validation and reply work are excluded.
 * No file addresses, names or remote task information are exposed. */
_Static_assert(sizeof(struct profile_file_snapshot) == 176, "file profile layout");

struct profile_host_operation {
  uint64_t requests, failures, requested_bytes, completed_bytes;
  uint64_t short_transfers, eof;
  uint64_t submissions, completions, transport_failures;
  struct profile_duration publication, bsp_queue, worker_queue, service, resume, total;
  struct profile_duration transport, transport_failed;
};

struct profile_host_snapshot {
  uint64_t flags;
  struct profile_host_operation read, write;
};

/* HOST_BEGIN/SNAPSHOT/END require HOST authority, with independent caller-local
 * state and the same control rules as MEMORY/FILE. Only native host READ/WRITE
 * requests admitted to the scheduler are counted. Requested bytes are the
 * captured native request size; completed bytes require CALL_OK. Positive short
 * transfers and successful zero-byte reads of a nonzero request (EOF) are
 * counted separately.
 * Boundaries: preparation before wait setup, before publication locking, BSP
 * forwarding, worker service start/end and caller resumption. Service includes
 * lazy OPEN, protocol handling, transport waits and worker scheduling. A request
 * rejected before worker service has zero service time; worker queue then ends
 * at rejection. Initial syscall validation and caller reply copies are excluded.
 * Transport counts include lazy OPEN under the native request. Completions mean
 * valid used-ring completions, not successful FUSE replies. Transport duration
 * runs from immediately before submission to worker-observed completion; failed
 * published requests have a separate duration ending at failure observation,
 * before reset recovery. Rejections before submission have no transport count.
 * Queue/device/daemon/host/guest scheduling are combined; these are elapsed ns,
 * not CPU time. Service contains transport intervals; do not add them together.
 * No metadata-only requests, sync, deferred cleanup or remote caller activity.
 * No added clocks/allocations when disabled; sums saturate with SATURATED.
 * Snapshots contain no addresses, node IDs, names or handles. */
_Static_assert(sizeof(struct profile_host_operation) == 200, "host operation layout");
_Static_assert(sizeof(struct profile_host_snapshot) == 408, "host snapshot layout");

/* All operations are header-only. BEGIN/SNAPSHOT/END require MEMORY authority. They always
 * act on the calling process, even through copied/delegated handles. One
 * collection per process, initially disabled; children start disabled too.
 * BEGIN clears counters, enables collection and has no reply (BUSY if active).
 * SNAPSHOT returns current/last counters without resetting or stopping them.
 * END disables collection and returns final counters (BAD_REQUEST if inactive).
 * Closing a handle does not stop collection; END or process exit does.
 *
 * Only requests that reach the private address space are counted, including
 * failures there. Malformed/denied syscalls and other kernel memory work are
 * excluded. Times are elapsed monotonic ns, including instrumentation, NOT CPU
 * time. The operation runs in the caller's syscall on its own CPU. Service is
 * the address-space change itself; total runs from syscall entry through reply
 * copying, so total minus service is validation and copying. Counter
 * accumulation is excluded. No per-request clock reads when off.
 * Totals saturate at UINT64_MAX and set SATURATED. The reply contains no
 * addresses. Bad reply storage must not change collection state. */
_Static_assert(sizeof(struct profile_duration) == 16, "profile duration layout");
_Static_assert(sizeof(struct profile_memory_operation) == 64, "profile operation layout");
_Static_assert(sizeof(struct profile_snapshot) == 136, "profile snapshot layout");

#endif
