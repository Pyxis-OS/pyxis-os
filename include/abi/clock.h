#ifndef ABI_CLOCK_H
#define ABI_CLOCK_H

#include <abi/message.h>

#define CLOCK_RIGHT_READ (UINT64_C(1) << 0)
#define CLOCK_RIGHT_SLEEP (UINT64_C(1) << 1)
#define CLOCK_RIGHTS (CLOCK_RIGHT_READ | CLOCK_RIGHT_SLEEP)

#define CLOCK_NOW UINT64_C(1)
#define CLOCK_SLEEP_UNTIL UINT64_C(2)

/* NOW is header-only and replies with clock_reading. All clock grants refer
 * to the same monotonic epoch, established during boot, not a calendar date.
 * Units are nanoseconds, not a guarantee of resolution or wakeup precision.
 * Time while the VM is paused or suspended need not be included. */
struct clock_reading {
  uint64_t nanoseconds;
};

/* SLEEP_UNTIL blocks the calling task, replies with no bytes, and succeeds
 * immediately for a past deadline. It never completes before the deadline;
 * scheduling can delay resumption. No asynchronous timer or cancellation. */
struct clock_sleep_request {
  struct message_header header;
  uint64_t deadline_ns;
};

_Static_assert(sizeof(struct clock_reading) == 8, "clock reading layout");
_Static_assert(sizeof(struct clock_sleep_request) == 24, "clock sleep layout");

#endif
