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

/* BSP, IF=0. Returns one owned reference to the stateless space factory. */
struct kernel_object *space_factory_create(void);
/* Current user task, IF=0. Captures on the caller's CPU; the BSP creates the
 * space and its first process. May sleep, with no held spinlocks. */
struct syscall_result space_factory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
