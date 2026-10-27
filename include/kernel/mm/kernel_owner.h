#ifndef KERNEL_MM_KERNEL_OWNER_H
#define KERNEL_MM_KERNEL_OWNER_H

#include <stdbool.h>
#include <stddef.h>

struct task;
struct task_wait;

/* Caller-owned admission storage, retained on its fixed actor through release.
 * No spinlock is held by this token. Nested scopes are the SAME task (or the
 * permanent scheduler context), not merely the same CPU; release inner first. */
struct vm_kernel_owner {
  struct vm_kernel_owner *root, *next;
  struct task *actor;
  struct task_wait *wait;
  size_t cpu, depth;
  bool queued, bootstrap;
};

/* IF=0. Zeroed token. Try before any irreversible cleanup/publication effect.
 * A failed try changes nothing. Bootstrap before scheduler publication has its
 * explicit BSP-exclusive proof; runtime ownership is CPU-neutral/kernel-root. */
bool vm_kernel_owner_try(struct vm_kernel_owner *owner);
bool vm_kernel_owner_held(void); /* Short scalar query, preserves caller IF. */
void vm_kernel_owner_release(struct vm_kernel_owner *owner);

/* IF=1, retained task in kernel root; no mapping loan/short lock held. One
 * absolute 1 s admission deadline. Returns false unchanged on contention.
 * A cleanup actor keeps its whole record and retries admission beyond that
 * budget, without completing joined cleanup early. Both return with IF=1. */
bool vm_kernel_owner_acquire(struct vm_kernel_owner *owner);
void vm_kernel_owner_cleanup(struct vm_kernel_owner *owner);

/* BSP IF=0, immediately before publishing scheduler startup to APs. */
void vm_kernel_runtime_start(void);
bool vm_kernel_runtime(void);

#endif
