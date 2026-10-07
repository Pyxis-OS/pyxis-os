#ifndef ABI_POWER_H
#define ABI_POWER_H

#include <abi/message.h>

#define POWER_RIGHT_OFF (UINT64_C(1) << 0)
#define POWER_RIGHT_RESTART (UINT64_C(1) << 1)
#define POWER_RIGHTS (POWER_RIGHT_OFF | POWER_RIGHT_RESTART)

/* PROTOCOL_POWER. OFF needs the OFF right and RESTART the RESTART right. Both
 * take no request payload and no reply. The kernel holds every user task,
 * flushes and checkpoints each mounted native pool, then enters S5 or resets.
 * Success never returns to the caller. Failure releases the held tasks, keeps
 * the system running and returns the status:
 * - UNAVAILABLE without ACPI, or when the firmware does not power off;
 * - BUSY while another power operation runs;
 * - a pool's flush failure, such as IO or TIMED_OUT. */
#define POWER_OFF UINT64_C(1)
#define POWER_RESTART UINT64_C(2)

#endif
