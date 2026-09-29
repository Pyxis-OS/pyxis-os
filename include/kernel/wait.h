#ifndef KERNEL_WAIT_H
#define KERNEL_WAIT_H

struct task_wait;

/* Shared task metadata, never a private-stack pointer. One resource queue may
 * publish this link at a time. Detach under its lock before wake; after a timed
 * wait, detach any remaining link before reuse. Wakers never access it after
 * wake returns. */
struct task_wait_link {
  struct task_wait_link *next;
  struct task_wait *wait;
};

#endif
