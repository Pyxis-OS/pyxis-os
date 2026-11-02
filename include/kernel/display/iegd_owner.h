#ifndef KERNEL_DISPLAY_IEGD_OWNER_H
#define KERNEL_DISPLAY_IEGD_OWNER_H

#include <kernel/mm/types.h>

struct task_wait;
struct iegd_worker;

/* Embedded, stable storage owned by the Intel device. Only these services
 * mutate queue/lifetime fields. A callback may queue itself once more while
 * running; repeated notifications coalesce, as with the IEGD release work. */
struct iegd_work {
  struct iegd_worker *worker;
  struct iegd_work *next;
  void (*run)(struct iegd_work *work);
  struct task_wait *cancel_wait;
  bool pending, running, cancelling;
};

struct iegd_worker {
  struct iegd_work *first, *last, *current;
  struct task_wait *wait, *join_wait;
  bool started, closing, exited;
};

/* BSP/IF=0 after task_init. Initially zero, stable one-shot device-owned
 * storage. Creates a normal fixed BSP kernel task; failure leaves it unstarted.
 * Nothing starts automatically: the real display adapter starts this worker
 * and queues its preparation only after bootstrap resource reservation. */
enum mm_result iegd_worker_start(struct iegd_worker *worker);
/* BSP/IF=0, initially zero work. The callback runs on its worker with IF=1,
 * without a queue lock or VM owner. All callbacks/notifications stay on BSP. */
void iegd_work_init(struct iegd_work *work, struct iegd_worker *worker,
    void (*run)(struct iegd_work *work));
/* BSP/IF=0, including IRQ. Allocation-free; false means already pending,
 * cancelling, unstarted or closed. Running work may get one pending rerun. */
bool iegd_work_queue(struct iegd_work *work);
/* BSP kernel task, IF=1, kernel root, no VM owner/short lock/loan. Remove a
 * pending run and join any active callback. Do not cancel the current callback
 * from itself; only one canceller may join a work item. Refills must be closed
 * before using this as a storage-retirement proof. Reuse is allowed afterward.
 * Returns whether a pending run was removed, not whether a callback was active. */
bool iegd_work_cancel_sync(struct iegd_work *work);
/* BSP/IF=0. Close queue admission; previously queued work drains normally.
 * Does not disable device sources or prove hardware/scanout retirement. */
void iegd_worker_close(struct iegd_worker *worker);
/* Same task context as cancel, from outside this worker, one joining owner.
 * Close first. Return joins all callbacks and all access to the worker record;
 * scheduler-stack reclamation may follow. An unstarted worker has no task to
 * join. Device sources/IRQ attachment need their separate stop/detach proof. */
void iegd_worker_join(struct iegd_worker *worker);

/* Single static BSP IRQ attachment, initially detached. BSP outside its own
 * interrupt callback, IF=0; attach precedes
 * device delivery. Handler/context stay valid until matching detach. Handler
 * may acknowledge bounded status, update completion state, wake and enqueue;
 * no allocation, sleeping, logging or native spin_lock in interrupt entry. */
enum mm_result iegd_irq_attach(void (*handler)(void *context), void *context);
/* Same context. BSP/IF=0 excludes the previous top half and closes the permanent gate.
 * A late MSI sees no handler/context. This joins only software IRQ access;
 * it neither retracts sent MSIs nor proves DMA/scanout stopped. */
enum mm_result iegd_irq_detach(void *context);
/* Architecture dispatch only, BSP IRQ entry, IF=0; arch performs APIC EOI. */
void iegd_irq_interrupt(void);

#endif
