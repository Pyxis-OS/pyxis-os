#ifndef KERNEL_OBJECT_PIPE_H
#define KERNEL_OBJECT_PIPE_H

#include <abi/pipe.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct task_wait;
struct pipe_pair;

/* Published queue links live in task metadata, never a remote kernel stack. */
struct pipe_wait {
  struct pipe_wait *next;
  struct task_wait *wait;
};

struct pipe_end {
  struct kernel_object object;
  struct pipe_pair *pair;
  bool reader;
};

/* BSP, IF=0. Each returned end has one owned reference. */
bool pipe_pair_create(struct pipe_end **reader, struct pipe_end **writer);
struct kernel_object *pipe_service_create(void);

struct syscall_result pipe_service_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result pipe_call(struct pipe_end *end, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
