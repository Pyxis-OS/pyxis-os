#ifndef ABI_THREAD_H
#define ABI_THREAD_H

#include <abi/handle.h>
#include <abi/message.h>
#include <stddef.h>

/* Management always applies to the calling process, even after transfer. */
#define THREAD_RIGHT_MANAGE (UINT64_C(1) << 0)
/* Caller-only initial FS binding, without CREATE, EXIT or parking authority. */
#define THREAD_RIGHT_SET_FS (UINT64_C(1) << 1)
#define THREAD_CREATE UINT64_C(1)
#define THREAD_EXIT UINT64_C(2)
#define THREAD_SET_FS UINT64_C(3)
#define THREAD_ADDRESS_WAIT UINT64_C(4)
#define THREAD_ADDRESS_WAKE UINT64_C(5)
#define THREAD_ADDRESS_WORD32_BYTES UINT64_C(4)
#define THREAD_ADDRESS_WORD64_BYTES UINT64_C(8)
#define THREAD_ADDRESS_NO_DEADLINE UINT64_MAX

struct thread_create {
  uint64_t entry;
  uint64_t argument;
  uint64_t stack_identity;
  uint64_t context_identity;
  uint64_t fs_base;
};

struct thread_fs_context {
  uint64_t identity;
  uint64_t fs_base;
};

struct thread_address_wait {
  uint64_t address;
  uint64_t expected;
  uint64_t deadline;
  uint64_t word_size;
};

struct thread_address_wake {
  uint64_t address;
  uint64_t word_size;
  uint64_t maximum;
};

/* Send the complete zero-initialized message. Unused fields must be zero.
 * CREATE enters a caller-supplied executable trampoline with RDI=argument and
 * a 16-byte aligned RSP at the usable stack end. There is no return address:
 * the trampoline must EXIT explicitly, without rerunning process startup.
 * Stack and optional FS context use distinct guarded MEMORY identities;
 * backing stays pinned until native completion. FS=0 requires no context;
 * nonzero FS may point within or one past its usable context (negative offsets).
 * SET_FS binds only the current thread, once, to such a context; rebinding is
 * BUSY. Existing initial threads start with FS=0. No libc TCB layout is imposed.
 * EXIT records its machine-word value and never returns on success. If the last
 * member leaves via THREAD_EXIT without a prior process result, the process
 * completes as PROCESS_EXITED with status zero, regardless of the thread value.
 * Existing process EXIT/fault/termination still stops every sibling. */
struct thread_message {
  struct message_header header;
  union {
    struct thread_create create;
    struct thread_fs_context set_fs;
    struct thread_address_wait address_wait;
    struct thread_address_wake address_wake;
    uint64_t exit_value;
  } body;
};

struct thread_reply {
  handle_t completion;
};

struct thread_address_wake_reply {
  uint64_t woken;
};

/* Address parking requires MANAGE and applies only to the calling process's
 * ordinary VM-owned writable user RAM. word_size is 4 or 8, address is naturally
 * aligned, and a 32-bit expected value has no high bits. Unused fields are zero.
 * WAIT compares/enrolls atomically: mismatch returns WOULD_BLOCK without sleep,
 * wake returns OK, expiry TIMED_OUT and mapping withdrawal BAD_BUFFER. Deadline
 * is absolute monotonic nanoseconds; zero polls, UINT64_MAX waits indefinitely.
 * WAKE requires nonzero maximum (UINT64_MAX means all) and returns the count of
 * detached registrations, not ownership of a user lock. Clients use atomics and
 * recheck predicates. Mapping incarnations prevent wake of a reused address.
 * No mapping lease survives sleep. Every terminal path detaches before notifying;
 * process stop resumes kernel cleanup and never returns to userspace. A failed
 * WAKE reply does not undo wakes already committed. */

#define THREAD_COMPLETION_RIGHT_WAIT (UINT64_C(1) << 0)
#define THREAD_WAIT UINT64_C(1)
#define THREAD_EXITED UINT64_C(1)
#define THREAD_STOPPED UINT64_C(2)

/* WAIT sends only a header. Result is immutable/repeatable after kernel task
 * retirement and context-pin release. STOPPED has value zero; no fabricated
 * thread return value. Closing an observer never kills a thread or releases
 * caller-owned backing. WAIT-authorized observers support WAIT_COMPLETE. */
struct thread_result {
  uint64_t kind;
  uint64_t value;
};

_Static_assert(sizeof(struct thread_create) == 40, "thread create layout");
_Static_assert(offsetof(struct thread_message, body) == 16, "thread payload offset");
_Static_assert(sizeof(struct thread_message) == 56, "thread message layout");
_Static_assert(sizeof(struct thread_reply) == 8, "thread reply layout");
_Static_assert(sizeof(struct thread_result) == 16, "thread result layout");

#endif
