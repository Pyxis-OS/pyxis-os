#ifndef KERNEL_ENDPOINT_H
#define KERNEL_ENDPOINT_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct endpoint_pair;

struct endpoint {
  struct kernel_object object;
  struct endpoint_pair *pair;
  unsigned side;
};

/* BSP, IF=0. Returns one owned reference to each end, or NULLs on failure.
 * Both ends can call/receive/reply, subject to their granted rights. A last
 * release closes that side during BSP retirement and wakes its peer. The
 * shared allocation survives until both endpoint destructors have run. */
bool endpoint_pair_create(struct endpoint **first, struct endpoint **second);

/* Current process, IF=0, with a live handle reference and a checked protocol
 * tag. May block on the task's kernel stack; no locks span a context switch.
 * Each direction allows one outstanding request until its caller consumes
 * the reply or closure result. Shared request storage belongs to the pair. */
struct syscall_result endpoint_call(struct endpoint *endpoint, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
