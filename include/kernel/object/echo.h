#ifndef KERNEL_OBJECT_ECHO_H
#define KERNEL_OBJECT_ECHO_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

/* BSP/IF=0, one owned reference to interface-scoped echo authority. */
struct kernel_object *echo_create(void);
struct syscall_result echo_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
