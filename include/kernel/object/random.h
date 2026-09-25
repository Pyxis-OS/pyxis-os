#ifndef KERNEL_OBJECT_RANDOM_H
#define KERNEL_OBJECT_RANDOM_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

/* BSP/IF=0, owned reference. Hardware availability is checked by READ. */
struct kernel_object *random_create(void);
struct syscall_result random_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
