#ifndef KERNEL_OBJECT_NET_CONFIG_H
#define KERNEL_OBJECT_NET_CONFIG_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

/* BSP/IF=0. One owned reference to configuration authority for net0. */
struct kernel_object *net_config_create(void);
struct syscall_result net_config_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
