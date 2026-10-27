#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/mm/types.h>
#include <kernel/object/capability.h>
#include <kernel/object/execution_group.h>
#include <kernel/spinlock.h>
#include <kernel/process_vm.h>
#include <abi/process.h>

struct space;
struct environment_table;
struct vm_space;
struct private_allocation;
struct process_control;
struct readiness_account;
struct endpoint;
struct completion_gate;
struct bulk_gate;
struct clipboard_receiver;
struct execution_group;
struct task;

enum process_lifetime {
  PROCESS_PREPARING,
  PROCESS_SUBMITTED,
  PROCESS_RETIRED,
};

/* Embedded in task storage; lifetime_lock protects the link and task pointer. */
struct process_task_member {
  struct process_task_member *next;
  struct task *task;
};

/* Owns submitted lifetime independently of its user tasks. space is borrowed
 * from the initialized set and must outlive the process. */
struct process {
  struct readiness_account *readiness_account; /* Owned process metadata budget. */
  struct space *space;
  struct vm_space *address_space;
  struct process_vm_activity vm;
  struct capability_table capabilities;
  struct environment_table *environment; /* Owns named grants/text after activity drains. */
  struct process_control *control; /* Owned reference through final reclamation. */
  struct execution_group *execution_group; /* Owned storage through member completion. */
  struct execution_group_member group_member;
  struct clipboard_receiver *paste_receiver; /* Scoped process-owned line reader. */
  struct endpoint *endpoints; /* BSP-owned weak list of receiving endpoints. */
  _Atomic(struct completion_gate *) completion_gate; /* Independently owned async admission. */
  _Atomic(struct bulk_gate *) bulk_gate; /* Data charge survives outstanding buffer owners. */
  struct private_allocation *allocations; /* Owned MEMORY/JIT regions, distinct kinds. */
  /* Process VM ownership protects gauges; live roots/borrowers provide snapshots. */
  size_t jit_bytes, jit_regions, jit_writable_bytes, jit_executable_bytes;
  uintptr_t startup_address; /* Read-only record in address_space; zero until prepared. */
  /* Control/group -> lifetime -> parking bucket -> scheduler queues. No allocation or context
   * switch under this lock; detach control/group links only after unlocking. */
  struct spinlock lifetime_lock;
  enum process_lifetime lifetime;
  struct process_task_member *tasks; /* Stop targets, detached before storage reclamation. */
  size_t task_count; /* Linked tasks, including those awaiting BSP retirement. */
  size_t task_storage_count; /* Includes detached storage and context pins until completion. */
  size_t cleanup_pins; /* lifetime_lock: retained cleanup borrowing process records. */
  bool stopping;
  /* One packed first-event election, including fault entry without lifetime_lock. */
  atomic_uint_fast64_t terminal_result;
  /* RETIRED and owned by the BSP reaper: its queue link, and the detached
   * control and group membership completed after release. */
  struct process *retired_next;
  struct process_control *retired_control;
  struct execution_group *retired_group;
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

/* BSP, IF=0, shared kernel structural owner held, exclusive unsubmitted
 * process, with no prepared task storage.
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
void process_task_attach(struct process *process, struct process_task_member *member,
                         struct task *task);
void process_task_discard(struct process *process, struct process_task_member *member);
/* BSP, before ready-queue publication. Transfers process lifetime to finalization. */
void process_publish(struct process *process);

/* BSP, IF=0, lifetime_lock held. Adds a sibling only while admission is open;
 * retained storage, including retiring threads, counts toward the tunable bound.
 * The caller retains the guard through runnable publication and separately
 * serializes group stopping. Group SEAL does not close internal admission.
 * Failure leaves the task and unused member with the caller. */
enum call_status process_thread_attach_locked(struct process *process,
    struct process_task_member *member, struct task *task);

/* IF=0, submitted or retiring process. Caller keeps it live through its task
 * or a locked control/group link. No observer or group stop link is usable
 * before task attachment/publication. Closes sibling admission before sampling
 * stop targets and preserves the first terminal result. Outside fault entry. */
void process_request_stop(struct process *process);
/* IF=0, submitted process, including user fault entry. Atomically elects the
 * first process-wide terminal result; later events leave it immutable. No lock,
 * allocation or peer stop fanout. Fault callers defer fanout to a safe stack. */
void process_set_result(struct process *process, struct process_result result);
/* Caller keeps process storage live. Acquire-observe immutable election;
 * get returns false and clears *result when no event has been committed. */
bool process_result_committed(struct process *process);
bool process_result_get(struct process *process, struct process_result *result);

/* BSP scheduler after the task has left its root/stack and returned every loan.
 * Detach the stop target, reclaim task storage, release context pins and publish
 * native thread completion, then call reclaimed. The last detach elects EXITED/0
 * only when no process-wide event exists and closes VM admission. For the last
 * storage, reclaimed returns true and queues the process for the reaper only
 * when no cleanup pins remain; the caller must not access it afterwards. The
 * final task storage or cleanup pin marks RETIRED and detaches control/group
 * membership before queueing without allocation. The reaper releases
 * the address space in bounded interrupts-off steps, then the remaining
 * resources, and only then publishes control and group completion. */
void process_task_detach(struct process *process, struct process_task_member *member);
bool process_task_reclaimed(struct process *process);
/* IF=0, caller already keeps the submitted process live. Retain borrowed
 * process records through whole-record cleanup after the syscall returns.
 * Pin and unpin are any CPU while task storage remains. The final unpin after
 * all task storage is gone uses the existing BSP finalization owner. Process,
 * control and group completion wait for both task storage and these pins.
 * No access to process is permitted after unpin without another live owner. */
bool process_cleanup_pin(struct process *process);
void process_cleanup_unpin(struct process *process);
/* BSP, IF=0, after task_init(). Starts the BSP process reaper task. */
void process_reaper_init(void);
/* Any CPU, IF=0, after releasing the producer's retirement queue guard. Wake
 * the existing reaper for retained cleanup; no allocation or context switch. */
void process_cleanup_notify(void);

#endif
