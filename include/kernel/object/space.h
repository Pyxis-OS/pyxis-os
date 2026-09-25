#ifndef KERNEL_OBJECT_SPACE_H
#define KERNEL_OBJECT_SPACE_H

#include <abi/space.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct space;

/* BSP, IF=0. Returns one owned reference bound to a boot-lifetime space. */
struct kernel_object *space_control_create(struct space *space);
/* Current user task, IF=0. Does not allocate or block. */
struct syscall_result space_control_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size);

#endif
