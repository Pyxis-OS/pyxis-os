#ifndef KERNEL_OBJECT_H
#define KERNEL_OBJECT_H

#include <stddef.h>
#include <stdatomic.h>

/* Embed in a resource whose lifetime is shared by kernel owners and handles.
 * Only the reference count and retirement link cross CPUs. Payload access has
 * its own synchronization rules; reference ownership alone does not lock it. */
struct kernel_object {
  atomic_size_t references;
  struct kernel_object *retired_next;
  void (*destroy)(struct kernel_object *);
};

/* Initialize before publication with one owned reference. The callback runs
 * only on the BSP with IF=0, outside the retirement lock. It releases the
 * enclosing allocation and owned resources, and must not borrow a process or
 * capability entry that may already have been destroyed. */
void object_init(struct kernel_object *object,
                 void (*destroy)(struct kernel_object *));

/* Caller owns a live reference throughout retain. False means count overflow;
 * no reference is acquired. Release consumes one owned reference. IF=0 for
 * release on every CPU; the last release queues destruction without allocating. */
bool object_retain(struct kernel_object *object);
void object_release(struct kernel_object *object);

/* Scheduler helpers, IF=0. Reaping is BSP-only; callbacks run outside the lock.
 * Pending work must also bring a busy BSP user task back to its scheduler. */
bool object_reap_pending(void);
void object_reap(void);

#endif
