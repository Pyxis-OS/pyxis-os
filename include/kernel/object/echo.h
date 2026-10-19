#ifndef KERNEL_OBJECT_ECHO_H
#define KERNEL_OBJECT_ECHO_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct syscall_result echo_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
