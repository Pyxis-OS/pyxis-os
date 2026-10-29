#ifndef KERNEL_PROCESS_VM_H
#define KERNEL_PROCESS_VM_H

#include <kernel/spinlock.h>
#include <arch/smp.h>
#include <stddef.h>
#include <stdint.h>

struct process;
struct task;
struct bsp_request;
struct memory_request;
struct task_wait;

struct process_vm_root_epoch {
  uint64_t incarnation, generation;
};

enum process_vm_phase {
  PROCESS_VM_OPEN,
  PROCESS_VM_DRAINING,
  PROCESS_VM_MUTATING,
  PROCESS_VM_REPLY,
  PROCESS_VM_RETIRED,
};

enum process_vm_external_state {
  PROCESS_VM_EXTERNAL_IDLE,
  PROCESS_VM_EXTERNAL_QUEUED,
  PROCESS_VM_EXTERNAL_OWNED,
  PROCESS_VM_EXTERNAL_OWNED_FINAL,
};

/* Embedded in the shared module's persistent per-process work record. The
 * process VM guard owns every field while attached. The shared owner retains
 * the process cleanup pin and token storage through external_finish(). */
struct process_vm_external {
  struct process *process;
  struct task *actor;
  size_t cpu;
  enum process_vm_external_state state;
};

enum process_vm_external_progress {
  PROCESS_VM_EXTERNAL_WAIT,
  PROCESS_VM_EXTERNAL_READY,
  PROCESS_VM_EXTERNAL_FINAL,
  PROCESS_VM_EXTERNAL_EXHAUSTED,
};

/* Embedded in task storage; vm_lock owns the link, scheduler queues own parking. */
struct process_vm_held {
  struct process_vm_held *next;
  struct task *task;
};

struct process_vm_activity {
  /* May nest inside process lifetime/object locks and outside scheduler queues.
   * Never allocate, copy, wait or switch roots while holding this guard. */
  struct spinlock lock;
  _Atomic(enum process_vm_phase) phase;
  size_t roots, borrowers;
  /* LOCK protects the live root reservations, including entry before CR3.
   * The incarnation is globally non-reused; generation is non-reused within
   * it. Neither a reused process address nor a reused CR3 identifies an epoch. */
  uint64_t incarnation, generation;
  uint64_t resident_cpus[(ARCH_CPU_LIMIT + 63) / 64];
  struct process_vm_held *held;
  struct task *mutator;
  struct bsp_request *pending; /* Retained caller-owned local transaction. */
  uint64_t deadline;
  size_t mutation_cpu; /* Exclusive owner, set before MUTATING; never a CPU0 assumption. */
  struct task_wait *local_wait; /* VM guard joins timeout detachment and notification. */
  struct process_vm_external *external; /* Shared-owned aggregate, independent of private reply. */
};

/* Scheduler, IF=0. Reserve before CR3 activation, acknowledge after kernel CR3.
 * Refusal atomically parks the task on a separate process-owned held list. */
bool process_vm_dispatch(struct process *process, struct task *task,
                          struct process_vm_root_epoch *epoch);
void process_vm_root_depart(struct process *process, struct process_vm_root_epoch epoch);
bool process_vm_execution_closed(struct process *process);

/* Current task, IF=0. Admission is nonblocking; a refused entry must switch out
 * before retrying. Previously admitted continuations may drain through closure. */
bool process_vm_syscall_enter(struct process *process, struct task *task);
void process_vm_syscall_leave(struct process *process, struct task *task);

/* Nonblocking, IF=0. Nestable mapping leases cover validation AND copy or a
 * nonblocking commit/final-copy interval. Never retain one across an I/O wait. */
bool process_vm_borrow_acquire(struct process *process);
void process_vm_borrow_release(struct process *process);

/* Admitted private MEMORY/JIT/thread/DISPLAY caller, IF=0; retained local record,
 * no BSP publication. Leaves its root, drains under the common gate, executes
 * and rejoins for reply. Any fixed-owner control uses separate request storage. */
void process_vm_private_call(struct bsp_request *request);
void process_vm_reply_finish(struct process *process);
void process_vm_require_mutation(struct process *process);
/* Exclusive mutation token only. Revalidate an output after draining, before effects;
 * replies prechecked by the caller may have changed before gate closure. */
bool process_vm_buffer_check(struct process *process, uintptr_t address,
                             size_t bytes, bool write);

/* IF=0, kernel root, no shared queue/forwarder guard. Attach one zero-initialized
 * aggregate token, retaining process/storage independently before this call.
 * CPU is its immutable fixed RAM-actor destination. Existing private ownership
 * and mutator-only reply finish before external ownership can be acquired. */
void process_vm_external_request(struct process *process,
    struct process_vm_external *external, size_t cpu);
/* Recorded fixed RAM actor, IF=0, kernel root. Never sleeps: WAIT leaves the
 * token queued for readiness notification. READY owns the drained live space
 * with a fresh generation; FINAL owns permanently closed space. EXHAUSTED keeps
 * admission closed and the token queued for termination/final retirement.
 * Repeated acquisition by the same owner does not spend another generation. */
enum process_vm_external_progress
process_vm_external_acquire(struct process_vm_external *external);
void process_vm_require_external(struct process_vm_external *external);
/* Owning fixed RAM actor, IF=0. Finish all PTE work before releasing ownership.
 * REOPEN is false only for a target whose stop has already been requested.
 * Permanent retirement never reopens. Detaches notification ownership and resets
 * the token; afterward its owner may release the process pin/storage. Returns
 * whether the target was permanently retired at detachment under the guard. */
bool process_vm_external_finish(struct process_vm_external *external, bool reopen);
/* Shared-module implementation. Called synchronously under the process VM
 * guard: only deduplicate/queue preallocated work and notify its fixed actor.
 * Never enter VM/lifetime guards, allocate or wait. The shared queue guard must
 * be released before calling a process VM API. Detachment joins this hook. */
void shared_process_vm_ready(struct process_vm_external *external);

/* Stop targeting may remove a held task before scheduler-side termination. */
void process_vm_unhold(struct process *process, struct task *task);
/* Exclusive unpublished preparation or drained final reaping, IF=0; closes
 * admission permanently after all root/borrower/continuation activity ends.
 * Idempotent. An attached external token remains owned and is notified when
 * queued; an acquired token keeps its actor and becomes final-space ownership. */
void process_vm_retire(struct process *process);
/* Zero-initialized unpublished process. False on root-incarnation exhaustion. */
bool process_vm_activity_init(struct process *process);

#endif
