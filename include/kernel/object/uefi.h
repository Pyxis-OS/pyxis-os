#ifndef KERNEL_OBJECT_UEFI_H
#define KERNEL_OBJECT_UEFI_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct kernel_object *uefi_create(void);
struct syscall_result uefi_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size, size_t reply_capacity);

#endif
