#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/mm/types.h>
#include <kernel/object/capability.h>
#include <kernel/object/execution_group.h>
#include <kernel/spinlock.h>
#include <abi/process.h>

struct space;
struct vm_space;
struct private_allocation;
struct process_control;
struct endpoint;
struct execution_group;
struct task;

enum process_lifetime {
  PROCESS_PREPARING,
  PROCESS_SUBMITTED,
  PROCESS_RETIRED,
};

/* Owns submitted lifetime independently of its sole user task. space is borrowed
 * from the initialized set and must outlive the process. */
struct process {
  struct space *space;
  struct vm_space *address_space;
  struct capability_table capabilities;
  struct process_control *control; /* Owned reference through final reclamation. */
  struct execution_group *execution_group; /* Owned storage through member completion. */
  struct execution_group_member group_member;
  struct endpoint *endpoints; /* BSP-owned weak list of receiving endpoints. */
  struct private_allocation *allocations; /* Private-memory service regions only. */
  uintptr_t startup_address; /* Read-only record in address_space; zero until prepared. */
  /* Control/group -> lifetime -> scheduler queues. No allocation or context
   * switch under this lock; detach control/group links only after unlocking. */
  struct spinlock lifetime_lock;
  enum process_lifetime lifetime;
  struct task *task; /* Stop target; detached before task storage is reclaimed. */
  bool task_storage; /* Stays true through detached kernel-stack/metadata reclamation. */
  bool stopping;
  /* Sole task writes result before release-publishing result_set, including on
   * user fault entry, where taking lifetime_lock is forbidden. */
  atomic_bool result_set;
  struct process_result result;
};

/* Borrow the executing user task's process; NULL for scheduler/kernel tasks.
 * Scheduler must be initialized on this CPU. IF=0, kernel GS active, outside
 * interrupt/fault entry. The pointer cannot outlive this execution context. */
struct process *process_current(void);

/* BSP, IF=0, after heap and space initialization. The caller exclusively owns
 * an inactive private address space. Success transfers it to the new process;
 * failure leaves it with the caller and clears *result when non-NULL. */
enum mm_result process_create(struct space *space, struct vm_space *address_space,
                              struct process **result);

/* BSP, IF=0, exclusive unsubmitted process, with no prepared task storage.
 * Submitted processes finalize through process_task_reclaimed(). Frees the address
 * space, private allocation records, capability table and process. Owned
 * keyboard, graphics and audio sessions are released first; if VM destruction fails,
 * the remaining resources stay intact. In-flight presentation may retain only pixel backing.
 * Audio exit invalidates ownership without allocation; its worker retains queue
 * storage and an attributed cleanup token, never the retired process.
 * Object references are released for BSP reaping; the owning space survives.
 * Detaches the control's borrowed process link without publishing completion.
 * Unpublished destruction only releases group storage. */
enum mm_result process_destroy(struct process *process);

/* BSP, IF=0, exclusive preparation. Exactly one task may be prepared, including
 * a blocked or not-yet-published task. Failure leaves both objects with caller. */
bool process_task_available(struct process *process);
void process_task_attach(struct process *process, struct task *task);
void process_task_discard(struct process *process, struct task *task);
/* BSP, before ready-queue publication. Transfers process lifetime to finalization. */
void process_publish(struct process *process);

/* IF=0, submitted or retiring process. Caller keeps it live through its task
 * or a locked control/group link. No observer or group stop link is usable
 * before task attachment/publication. Stop preserves committed exit/fault. */
void process_request_stop(struct process *process);
/* Sole task's exit boundary, IF=0, including user fault entry. Writes once;
 * publication lets stop targeting observe an immutable committed result. */
void process_set_result(struct process *process, struct process_result result);

/* BSP reaper after the task has left its root/stack and returned every loan.
 * Detach the stop target, then reclaim task storage, then call reclaimed.
 * The final call destroys the process and publishes completion; no access after. */
void process_task_detach(struct process *process, struct task *task);
void process_task_reclaimed(struct process *process);

#endif
