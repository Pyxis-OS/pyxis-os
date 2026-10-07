#ifndef KERNEL_USER_WAIT_H
#define KERNEL_USER_WAIT_H

#include <abi/wait.h>
#include <kernel/service/request.h>
#include <kernel/syscall.h>
#include <stddef.h>

struct kernel_object;
struct process;

struct readiness_interest {
  struct kernel_object *object;
  uint64_t events;
  uint64_t observed_generation;
  uint64_t ready;
};

/* One call owns these references until its worker unlinks and completes it.
 * Results live in the task's shared request area, never a private user stack. */
struct readiness_request {
  struct bsp_request request;
  /* Borrowed until completion: the sole caller task cannot retire while its
   * published request remains outstanding. Cleared before waking that task. */
  struct process *caller;
  size_t count;
  uint64_t deadline;
  enum call_status status;
  struct readiness_interest interests[WAIT_MAX_INTERESTS];
};

struct syscall_result user_wait_many(uintptr_t interests, uint64_t count,
    uint64_t deadline, uintptr_t output);
/* BSP, IF=0, after task_init and before publishing request producers. */
void readiness_init(void);
/* Common BSP executor, IF=0. Transfers a FORWARDED request to the independent
 * readiness worker, or the network worker when any interest needs TCP state. */
void readiness_submit(struct readiness_request *request);
/* Any CPU, IF=0, after publishing readiness state and releasing its lock. A
 * notification is remembered across observation and worker wait publication. */
void readiness_notify(void);

#endif
