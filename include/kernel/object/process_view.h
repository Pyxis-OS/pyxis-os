#ifndef KERNEL_OBJECT_PROCESS_VIEW_H
#define KERNEL_OBJECT_PROCESS_VIEW_H

#include <abi/process_view.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

/* BSP/IF=0: an owned reference to the system-wide process view. */
struct kernel_object *process_view_create(void);
/* Current user syscall, IF=0: LIST over every space. */
struct syscall_result process_view_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
/* Current user syscall, IF=0: the caller's own space, with no handle. */
struct syscall_result space_processes_call(uintptr_t reply_address, uint64_t reply_capacity);

#endif
