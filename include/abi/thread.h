#ifndef ABI_THREAD_H
#define ABI_THREAD_H

#include <abi/handle.h>
#include <abi/message.h>
#include <stddef.h>

/* Management always applies to the calling process, even after transfer. */
#define THREAD_RIGHT_MANAGE (UINT64_C(1) << 0)
#define THREAD_CREATE UINT64_C(1)
#define THREAD_EXIT UINT64_C(2)
#define THREAD_SET_FS UINT64_C(3)

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

/* Send the complete zero-initialized message. Unused fields must be zero.
 * CREATE enters a caller-supplied executable trampoline with RDI=argument and
 * a 16-byte aligned RSP at the usable stack end. There is no return address:
 * the trampoline must EXIT explicitly, without rerunning process startup.
 * Stack and optional FS context use distinct guarded MEMORY identities;
 * backing stays pinned until native completion. FS=0 requires no context;
 * nonzero FS may point within or one past its usable context (negative offsets).
 * SET_FS binds only the current thread, once, to such a context; rebinding is
 * BUSY. Existing initial threads start with FS=0. No libc TCB layout is imposed.
 * EXIT records its machine-word value and never returns on success. Existing
 * process EXIT/fault/termination still stops every sibling. */
struct thread_message {
  struct message_header header;
  union {
    struct thread_create create;
    struct thread_fs_context set_fs;
    uint64_t exit_value;
  } body;
};

struct thread_reply {
  handle_t completion;
};

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
