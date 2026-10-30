#ifndef KERNEL_MM_SHARED_VIEW_H
#define KERNEL_MM_SHARED_VIEW_H

#include <abi/memory.h>
#include <abi/syscall.h>

struct process;
struct shared_region;
struct shared_view;

/* Immutable owner identity, while persistent registration holds the view. */
bool shared_view_targets(struct shared_view *view, struct process *process);

/* Caller-owned drained private mutation, IF=0. Full-region R/NX or RW/NX;
 * enrollment precedes root exposure. No user pointer or handle is retained. */
enum call_status shared_view_map(struct process *process,
    struct shared_region *region, bool writable, struct memory_reservation *result);
/* Same mutation owner. False means the identity is not a shared view. */
bool shared_view_release(struct process *process, uint64_t identity);
/* Last task's proved departure, after process_vm_retire; queues, never waits. */
void shared_process_exit(struct process *process);
/* Final process record owner, after joined alias removal and VM destruction. */
void shared_process_discard(struct process *process);

/* Existing fixed RAM actor only. Ready queue and timers own process pins;
 * pending/deadline are lock-free observations for the actor's sleep handshake. */
void shared_views_init(void);
bool shared_views_pending(void);
uint64_t shared_views_deadline(void);
void shared_views_reap(void);

#endif
