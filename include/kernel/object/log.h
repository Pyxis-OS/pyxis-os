#ifndef KERNEL_OBJECT_LOG_H
#define KERNEL_OBJECT_LOG_H

#include <kernel/object/object.h>
#include <kernel/syscall.h>

/* BSP, IF=0. One owned reference to stateless read-only log authority. The
 * static ring outlives this object and every delegated/copy handle. */
struct kernel_object *log_create(void);
struct syscall_result log_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
