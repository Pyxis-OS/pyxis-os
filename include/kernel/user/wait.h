#ifndef KERNEL_USER_WAIT_H
#define KERNEL_USER_WAIT_H

#include <abi/wait.h>
#include <kernel/service/request.h>
#include <kernel/syscall.h>
#include <stddef.h>

struct kernel_object;

struct readiness_interest {
  struct kernel_object *object;
  uint64_t events;
  uint64_t ready;
};

/* One call owns these references until the worker unlinks and completes it.
 * Results live in the task's shared request area, never a private user stack. */
struct readiness_request {
  struct bsp_request request;
  size_t count;
  uint64_t deadline;
  enum call_status status;
  struct readiness_interest interests[WAIT_MAX_INTERESTS];
};

struct syscall_result user_wait_many(uintptr_t interests, uint64_t count,
    uint64_t deadline, uintptr_t output);
/* Common BSP executor, IF=0: transfers ownership to the network worker. */
void net_readiness_submit(struct readiness_request *request);

#endif
