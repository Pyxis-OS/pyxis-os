#ifndef ABI_PROCESS_H
#define ABI_PROCESS_H

#include <abi/message.h>
#include <abi/syscall.h>
#include <stdbool.h>

#define PROCESS_RIGHT_WAIT (UINT64_C(1) << 0)
#define PROCESS_RIGHT_TERMINATE (UINT64_C(1) << 1)
#define PROCESS_RIGHTS (PROCESS_RIGHT_WAIT | PROCESS_RIGHT_TERMINATE)
#define PROCESS_WAIT UINT64_C(1)
#define PROCESS_TERMINATE UINT64_C(2)

#define PROCESS_EXITED UINT64_C(1)
#define PROCESS_FAULTED UINT64_C(2)
#define PROCESS_TERMINATED UINT64_C(3)
#define PROCESS_START_FAILED UINT64_C(4)

#define PROCESS_START_RECORD 1
#define PROCESS_START_MEMORY 2
#define PROCESS_START_TLS 3
#define PROCESS_START_FS 4
#define PROCESS_START_RESOURCES 5
#define PROCESS_START_STDIO_SNAPSHOT 6
#define PROCESS_START_STDIN 7
#define PROCESS_START_STDOUT 8
#define PROCESS_START_STDERR 9
#define PROCESS_START_STDIO_CLOSE 10

#define PROCESS_START_STEP_SHIFT 16
#define PROCESS_START_STATUS_MASK UINT32_C(0xffff)
#define PROCESS_START_UNKNOWN_CALL UINT32_C(0xffff)

/* START_FAILED's positive 32-bit status packs step in bits 16..30 and the
 * failing call status in bits 0..15. UNKNOWN_CALL encodes the all-ones native
 * unknown-syscall result; zero is never a failure. Programs report their own
 * startup failures, just as they report their own ordinary exit status. */
static inline bool process_start_failure_valid(int64_t reason)
{
  if (reason < 0 || reason > INT32_MAX) {
    return false;
  }
  uint32_t step = (uint32_t)reason >> PROCESS_START_STEP_SHIFT;
  uint32_t status = (uint32_t)reason & PROCESS_START_STATUS_MASK;
  return step >= PROCESS_START_RECORD && step <= PROCESS_START_STDIO_CLOSE &&
      ((status > CALL_OK && status < CALL_STATUS_COUNT) ||
       status == PROCESS_START_UNKNOWN_CALL);
}

/* WAIT sends only a message_header. Completion is immutable and repeatable,
 * and becomes visible after execution resources have been reclaimed. Status
 * is the sign-extended 32-bit exit code for EXITED, the packed startup reason
 * for START_FAILED, and zero for FAULTED/TERMINATED. All kinds share the same
 * first-result election, stop sibling tasks and publish only after retirement.
 * If the last member leaves via THREAD_EXIT without a prior process result,
 * completion is PROCESS_EXITED with status zero, regardless of the thread value.
 * Fault details remain in the kernel log. Closing a handle does not stop
 * execution.
 * WAIT-authorized observers also accept WAIT_COMPLETE through WAIT_MANY. The
 * readiness event does not consume the result; WAIT retrieves it immediately. */
struct process_result {
  uint64_t kind;
  int64_t exit_status;
};

/* TERMINATE sends only a message_header, requires TERMINATE and has no reply.
 * It requests that the observed process stop, using the same safe-stop path as
 * execution-group termination, and returns without waiting. It is idempotent
 * and succeeds after completion too: an exit or fault already committed keeps
 * its result; otherwise the result becomes TERMINATED. It affects only that
 * process, not processes it launched. Observe completion with WAIT. */

_Static_assert(sizeof(struct process_result) == 16, "process completion layout");

#endif
