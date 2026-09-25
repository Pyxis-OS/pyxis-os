#ifndef KERNEL_OBJECT_TCP_H
#define KERNEL_OBJECT_TCP_H

#include <kernel/object/object.h>
#include <kernel/syscall.h>

struct kernel_object *tcp_service_create(void);
struct syscall_result tcp_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result tcp_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t request_size, uintptr_t reply_address, size_t reply_capacity);

#endif
