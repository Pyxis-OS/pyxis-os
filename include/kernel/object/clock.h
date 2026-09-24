#ifndef KERNEL_OBJECT_CLOCK_H
#define KERNEL_OBJECT_CLOCK_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

/* Stateless authority to read the shared clock and/or sleep the caller.
 * Creation is BSP-only, IF=0; returns one owned reference. */
struct kernel_object *clock_create(void);
struct syscall_result clock_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
