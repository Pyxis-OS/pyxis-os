#ifndef KERNEL_MEMORY_SERVICE_H
#define KERNEL_MEMORY_SERVICE_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

/* BSP, IF=0. Returns one owned reference, or NULL. This stateless authority
 * object owns no process or mappings, and may be shared/transferred normally. */
struct kernel_object *memory_create(void);

/* Current user task, IF=0, live service reference and resolved rights. Captures
 * the payload, checks the allocation reply, then lends the inactive address
 * space to BSP through its scheduler. Only the resumed caller copies the reply.
 * RELEASE has no reply buffer and may unmap its captured request itself. */
struct syscall_result memory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
