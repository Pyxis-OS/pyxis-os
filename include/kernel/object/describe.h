#ifndef KERNEL_OBJECT_DESCRIBE_H
#define KERNEL_OBJECT_DESCRIBE_H

#include <abi/syscall.h>
#include <stdint.h>

/* Provider-free current-process snapshot. No allocation or scheduling. */
struct syscall_result describe_call(uint64_t handle, uint64_t flags,
    uintptr_t destination);

#endif
