#ifndef ABI_WAIT_H
#define ABI_WAIT_H

#include <abi/handle.h>

/* General per-call bound. Input, object and result arrays use about 40 bytes
 * of kernel stack per interest. The current 4928-byte task request area is
 * sized by HOST; 32 readiness interests fit without increasing that area.
 * The readiness worker rescans interests when notified. */
#define WAIT_MAX_INTERESTS UINT64_C(32)
#define WAIT_MAX_WAIT_NS UINT64_C(30000000000)
#define WAIT_READABLE (UINT64_C(1) << 0)
#define WAIT_WRITABLE (UINT64_C(1) << 1)
#define WAIT_ACCEPTABLE (UINT64_C(1) << 2)
#define WAIT_PEER_FIN (UINT64_C(1) << 3)
#define WAIT_WRITE_CLOSED (UINT64_C(1) << 4)
#define WAIT_CLOSED (UINT64_C(1) << 5)
#define WAIT_ERROR (UINT64_C(1) << 6) /* Output only; automatic for every interest. */
#define WAIT_COMPLETE (UINT64_C(1) << 7)
#define WAIT_INTERRUPT (UINT64_C(1) << 8)
#define WAIT_RESIZED (UINT64_C(1) << 9)

struct wait_interest {
  handle_t handle;
  uint64_t events;
  uint64_t observed_generation; /* Used only by RESIZED; otherwise ignored. */
};

/* WAIT_MANY takes interests/count/deadline/output in RDI/RSI/RDX/R10.
 * Count is 1..32; output is count uint64_t event masks in input order, including
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
 * Console and terminal input READABLE require READ; terminal input also accepts
 * PEER_FIN and automatically includes it with READABLE after END_INPUT. Drain
 * buffered input before interpreting a zero-byte read as EOF. Ordinary input
 * readiness excludes an active reader and reserves no FIFO admission. Input
 * loss reports ERROR for console READABLE; terminal hangup reports ERROR.
 * INTERRUPT requires ARMED and observes the Ctrl+C latch without consuming it.
 * Console RESIZED requires READ or WRITE;
 * terminal input/output RESIZED requires READ/WRITE respectively. Explicit
 * terminal resize advances its geometry generation. Display RESIZED requires DRAW and
 * the caller's own space, without acquiring graphics. Keyboard and pointer accept
 * only READABLE with INPUT in the caller's own space and an acquired session.
 * Pointer state/reset events remain readable while unfocused; lost ownership
 * reports ERROR. Readiness never acquires input or reserves an event.
 * RESIZED reports observed_generation != current generation, level-triggered
 * and coalesced; re-query geometry to obtain its generation before waiting again.
 * Backend unavailability reports ERROR for display interests.
 * Process observers and execution groups accept only COMPLETE, requiring their
 * WAIT right. Completion is immutable and reports finished cleanup, not program
 * success. PROCESS_WAIT retrieves the observer's immutable result; group
 * completion additionally observes attributed deferred cleanup.
 * Process/group/terminal/console/display/keyboard/pointer/TCP interests may be
 * mixed; waits without TCP need no network device.
 * Poll and try operations may park for the BSP worker handoff, never for I/O
 * readiness. */

_Static_assert(sizeof(struct wait_interest) == 24, "wait interest layout");

#endif
