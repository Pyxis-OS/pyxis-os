#ifndef KERNEL_OBJECT_CLOCK_H
#define KERNEL_OBJECT_CLOCK_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct boot_info;
/* BSP only, once after the monotonic clock starts and before AP startup.
 * Copies the UTC seed and samples the monotonic anchor; uses no allocation. */
void clock_init(const struct boot_info *boot);

/* Initialization and timer entry only, using an already taken precise sample.
 * Publishes a shared monotonic maximum without allocation or hardware access. */
void clock_cache_update(uint64_t now);
/* Any CPU after initialization. May trail precise time; nominal freshness is
 * one serviced preemption period, with no bound across interrupt/VM pauses. */
uint64_t clock_coarse_ns(void);

/* Allocation-free kernel reading after initialization, on any CPU. False means
 * UTC is unavailable; otherwise signed Unix nanoseconds are clamped to int64. */
bool clock_wall_nanoseconds(int64_t *result);
/* Same UTC validity and representation, using the interrupt cache. Metadata
 * may repeat timestamps; no ordered hardware read is taken. */
bool clock_wall_coarse_nanoseconds(int64_t *result);

/* Stateless authority to read the shared clock and/or sleep the caller.
 * Creation is BSP-only, IF=0; returns one owned reference. */
struct kernel_object *clock_create(void);
struct syscall_result clock_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
