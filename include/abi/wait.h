#ifndef ABI_WAIT_H
#define ABI_WAIT_H

#include <abi/handle.h>

#define WAIT_MAX_INTERESTS UINT64_C(16)
#define WAIT_MAX_WAIT_NS UINT64_C(30000000000)
#define WAIT_READABLE (UINT64_C(1) << 0)
#define WAIT_WRITABLE (UINT64_C(1) << 1)
#define WAIT_ACCEPTABLE (UINT64_C(1) << 2)
#define WAIT_PEER_FIN (UINT64_C(1) << 3)
#define WAIT_WRITE_CLOSED (UINT64_C(1) << 4)
#define WAIT_CLOSED (UINT64_C(1) << 5)
#define WAIT_ERROR (UINT64_C(1) << 6) /* Output only; automatic for every interest. */

struct wait_interest {
  handle_t handle;
  uint64_t events;
};

/* WAIT_MANY takes interests/count/deadline/output in RDI/RSI/RDX/R10.
 * Count is 1..16; output is count uint64_t event masks in input order, including
 * zeros for unready entries. Success returns count*8 reply bytes, failure zero
 * and leaves output untouched. Inputs are copied before any output is written.
 *
 * Deadline is absolute monotonic time, at most 30 seconds ahead; zero polls.
 * Check current readiness first, even after a blocking deadline expires. If
 * none is ready, poll succeeds with zeros; an expired wait returns TIMED_OUT.
 * No registrations/references survive return. Readiness is level-triggered,
 * reserves nothing, and may be stale by the next transfer attempt.
 *
 * TCP READABLE/PEER_FIN require READ; WRITABLE/WRITE_CLOSED require WRITE.
 * READABLE automatically reports PEER_FIN; WRITABLE reports WRITE_CLOSED.
 * FIN can coexist with buffered data: drain reads before interpreting zero as
 * EOF. FIN alone never closes writes. A listener accepts ACCEPTABLE/CLOSED,
 * both requiring ACCEPT; ACCEPTABLE automatically reports CLOSED. Relevant
 * terminal errors report ERROR without an additional INSPECT right.
 * Unknown types/interests or zero masks are BAD_REQUEST; stale handles are
 * BAD_HANDLE and insufficient rights are DENIED. Validate the entire list
 * before registration. Repeated handles/copies are separate observations.
 * Terminal attachment READABLE/PEER_FIN require DRAIN (output records/EOF);
 * WRITABLE/WRITE_CLOSED require INJECT (input capacity/closure). Ordinary
 * interests include their respective closure flag; hangup reports ERROR.
 * Terminal application handles and framebuffer consoles are not wait targets.
 * Poll and try operations may park for the BSP worker handoff, never for I/O
 * readiness. Terminal-only waits do not require a network device. */

_Static_assert(sizeof(struct wait_interest) == 16, "wait interest layout");

#endif
