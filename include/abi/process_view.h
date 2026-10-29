#ifndef ABI_PROCESS_VIEW_H
#define ABI_PROCESS_VIEW_H

#include <abi/message.h>

/* Process listings. SYSCALL_SPACE_PROCESSES lists the caller's own space and
 * needs no handle. A process view object with LIST lists every space; boot
 * init receives one as bootstrap `process_view`. Both return the same reply:
 * a process_list_header followed by `returned` records.
 *
 * Membership and the group tree come from one pass per space; CPU time,
 * memory and state are samples taken during that pass. Rows hold no argv,
 * environment, working directory or capability. Keys are per-boot serials,
 * never reused, for display and correlating refreshes only: they are not
 * process IDs and no call accepts one. */
#define PROCESS_VIEW_RIGHT_LIST (UINT64_C(1) << 0)

/* message_header only; requires LIST. */
#define PROCESS_VIEW_LIST UINT64_C(1)

#define PROCESS_LIST_VERSION 1u
/* At most this many reply bytes are produced per call, header included. */
#define PROCESS_LIST_REPLY_MAX 65536u
#define PROCESS_LIST_LABEL_MAX 32u
#define PROCESS_LIST_SPACE_MAX 32u

/* The capacity did not hold every record; `total` is the full count. */
#define PROCESS_LIST_TRUNCATED (UINT64_C(1) << 0)
/* CPU times are zero because no TSC rate is known on this boot. */
#define PROCESS_LIST_CPU_UNAVAILABLE (UINT64_C(1) << 1)

#define PROCESS_LIST_GROUP 1u
#define PROCESS_LIST_PROCESS 2u

/* Process state, sampled. RUNNING includes runnable threads waiting for a
 * CPU; EXITING has a committed result or a stop request and still has
 * threads. Retired processes are absent. */
#define PROCESS_LIST_RUNNING 1u
#define PROCESS_LIST_WAITING 2u
#define PROCESS_LIST_EXITING 3u

/* Group flags, in the state field of a group record. */
#define PROCESS_LIST_GROUP_SEALED (1u << 0)
#define PROCESS_LIST_GROUP_STOPPING (1u << 1)
#define PROCESS_LIST_GROUP_COMPLETE (1u << 2)

struct process_list_header {
  uint32_t version; /* PROCESS_LIST_VERSION */
  uint32_t record_size; /* sizeof(struct process_list_record) */
  uint64_t total; /* Records the listing had, returned or not. */
  uint64_t returned; /* Records following this header. */
  uint64_t flags;
  uint64_t monotonic_ns; /* When the listing was taken. */
};

/* Strings are NUL-terminated; unused bytes are zero. */
struct process_list_record {
  uint32_t kind; /* PROCESS_LIST_GROUP or PROCESS_LIST_PROCESS */
  uint32_t state; /* Process state, or group flags. */
  uint64_t key;
  /* Process: its execution group's key. Group: its parent's key. 0: none. */
  uint64_t group_key;
  /* Process: the key of the process that launched it, information only; 0
   * when the kernel started it. Group: 0. */
  uint64_t launched_by;
  char space[PROCESS_LIST_SPACE_MAX]; /* The space's fixed name. */
  /* Process: argv[0]'s last path component, truncated; chosen by the
   * launching program, so it can be spoofed. Group: empty. */
  char label[PROCESS_LIST_LABEL_MAX];
  uint32_t threads; /* Process: live threads. Group: member processes. */
  uint32_t depth; /* Group: 1 for a root group. Process: 0. */
  uint64_t user_ns, kernel_ns; /* Process: CPU time so far. Group: 0. */
  /* Process: committed private memory (image, stacks, MEMORY and JIT
   * regions), not RSS; owned objects are not attributed. Group: 0. */
  uint64_t private_bytes;
  uint64_t start_ns; /* Process: monotonic time of publication. Group: 0. */
  uint64_t reserved;
};

_Static_assert(sizeof(struct process_list_header) == 40, "process list header layout");
_Static_assert(sizeof(struct process_list_record) == 144, "process list record layout");

#endif
