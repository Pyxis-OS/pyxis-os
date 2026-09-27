#ifndef KERNEL_NAMESPACE_H
#define KERNEL_NAMESPACE_H

#include <abi/namespace.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <stdbool.h>

struct process;

/* BSP, IF=0. Creation borrows an exclusively owned caller table. */
struct kernel_object *namespace_service_create(void);
enum call_status namespace_create(struct process *owner, handle_t *handle);

/* IF=0, caller holds a namespace reference. Presence includes dead exports;
 * this checks name ambiguity, not liveness or lookup authority. */
bool namespace_has_name(struct kernel_object *object, const char *name);

struct syscall_result namespace_service_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result namespace_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
