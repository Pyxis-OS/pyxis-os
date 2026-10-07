#ifndef KERNEL_OBJECT_POWER_H
#define KERNEL_OBJECT_POWER_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

/* BSP/IF=0, owned reference to stateless power-off and restart authority. */
struct kernel_object *power_create(void);
/* Current user task's syscall. Forwards to the ACPI worker through a BSP
 * request and returns only when the operation fails. */
struct syscall_result power_call(uint64_t rights, uint64_t operation,
    size_t request_size, size_t reply_capacity);

#endif
