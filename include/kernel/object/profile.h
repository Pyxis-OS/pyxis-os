#ifndef KERNEL_OBJECT_PROFILE_H
#define KERNEL_OBJECT_PROFILE_H

#include <abi/profile.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

/* BSP, IF=0. Stateless authority; measurements belong to the caller's task. */
struct kernel_object *profile_create(void);
struct syscall_result profile_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);

#endif
