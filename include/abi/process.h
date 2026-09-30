#ifndef ABI_PROCESS_H
#define ABI_PROCESS_H

#include <abi/message.h>

#define PROCESS_RIGHT_WAIT (UINT64_C(1) << 0)
#define PROCESS_WAIT UINT64_C(1)

#define PROCESS_EXITED UINT64_C(1)
#define PROCESS_FAULTED UINT64_C(2)
#define PROCESS_TERMINATED UINT64_C(3)

/* WAIT sends only a message_header. Completion is immutable and repeatable,
 * and becomes visible after execution resources have been reclaimed. Status
 * is the sign-extended 32-bit exit code for EXITED, zero for FAULTED/TERMINATED. Fault
 * details remain in the kernel log. Closing a handle does not stop execution. */
struct process_result {
  uint64_t kind;
  int64_t exit_status;
};

_Static_assert(sizeof(struct process_result) == 16, "process completion layout");

#endif
