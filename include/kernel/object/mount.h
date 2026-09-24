#ifndef KERNEL_OBJECT_MOUNT_H
#define KERNEL_OBJECT_MOUNT_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

/* BSP/IF=0: one owned reference to authority over the selected boot-lifetime
 * virtio-fs export. Creation does not wait for transport initialization. */
struct kernel_object *mount_create(void);
struct syscall_result mount_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);

#endif
