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

enum process_vm_phase {
  PROCESS_VM_OPEN,
  PROCESS_VM_DRAINING,
  PROCESS_VM_MUTATING,
  PROCESS_VM_REPLY,
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
  struct bsp_request *pending;
  struct process *next_pending; /* BSP VM worker list, IF=0. */
  uint64_t deadline;
  size_t mutation_cpu; /* Exclusive owner, set before MUTATING; never a CPU0 assumption. */
  struct task_wait *local_wait; /* VM guard joins timeout detachment and notification. */
};

/* Scheduler, IF=0. Reserve before CR3 activation, acknowledge after kernel CR3.
 * Refusal atomically parks the task on a separate process-owned held list. */
bool process_vm_dispatch(struct process *process, struct task *task, uint64_t *generation);
void process_vm_root_depart(struct process *process, uint64_t generation);
bool process_vm_execution_closed(struct process *process);

/* Current task, IF=0. Admission is nonblocking; a refused entry must switch out
 * before retrying. Previously admitted continuations may drain through closure. */
bool process_vm_syscall_enter(struct process *process, struct task *task);
void process_vm_syscall_leave(struct process *process, struct task *task);

/* Nonblocking, IF=0. Nestable mapping leases cover validation AND copy or a
 * nonblocking commit/final-copy interval. Never retain one across an I/O wait. */
bool process_vm_borrow_acquire(struct process *process);
void process_vm_borrow_release(struct process *process);

/* Executor transfers a request without waiting for drainage. The preallocated
 * record owns process/task lifetime through its uninterruptible caller. */
void process_vm_mutation_forward(struct bsp_request *request);
/* Admitted MEMORY caller, IF=0; uses its retained record without BSP publication.
 * Leaves its root, drains under the common gate, executes and rejoins for reply. */
void process_vm_memory_call(struct memory_request *request);
void process_vm_reply_finish(struct process *process);
void process_vm_require_mutation(struct process *process);
/* Exclusive mutation token only. Revalidate an output after draining, before effects;
 * replies prechecked by the caller may have changed before gate closure. */
bool process_vm_buffer_check(struct process *process, uintptr_t address,
                             size_t bytes, bool write);

/* Stop targeting may remove a held task before scheduler-side termination. */
void process_vm_unhold(struct process *process, struct task *task);
/* BSP exclusive preparation or final reaping; closes admission permanently. */
void process_vm_retire(struct process *process);
/* Zero-initialized unpublished process. False on root-incarnation exhaustion. */
bool process_vm_activity_init(struct process *process);
void process_vm_init(void);

#endif
