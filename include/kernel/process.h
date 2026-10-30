#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/mm/types.h>
#include <kernel/object/capability.h>
#include <kernel/object/execution_group.h>
#include <kernel/spinlock.h>
#include <kernel/process_vm.h>
#include <kernel/display_retire.h>
#include <abi/process.h>
#include <abi/process_view.h>

struct space;
struct shared_origin;
struct shared_process;
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
struct display_object;

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
  size_t departing_tasks; /* Attached tasks which have not left their final root/stack. */
  size_t task_storage_count; /* Includes detached storage and context pins until completion. */
  size_t cleanup_pins; /* lifetime_lock: retained cleanup borrowing process records. */
  struct task *preparation_actor; /* Exclusive unpublished owner, NULL at boot. */
  size_t preparation_cpu, retirement_cpu;
  bool preparation_transferred;
  enum {
    PROCESS_CLEANUP_SESSIONS,
    PROCESS_CLEANUP_DISPLAY_UNMAP,
    PROCESS_CLEANUP_DISPLAY_FINISH,
    PROCESS_CLEANUP_VM,
    PROCESS_CLEANUP_HOOKS,
    PROCESS_CLEANUP_RECORDS,
  } cleanup_phase;
  struct {
    struct display_object *display;
    uintptr_t address;
    size_t bytes;
    struct display_retirement retirement;
  } display_exit;
  bool stopping;
  /* One packed first-event election, including fault entry without lifetime_lock. */
  atomic_uint_fast64_t terminal_result;
  /* RETIRED and exclusively owned by its current fixed/RAM cleanup phase: its queue link, and the detached
   * control and group membership completed after release. */
  struct process *retired_next;
  struct process_control *retired_control;
  struct execution_group *retired_group;
  /* TSC cycles of retired tasks, added under lifetime_lock when the BSP
   * reaper detaches them; live tasks keep their own. */
  uint64_t cpu_user_cycles, cpu_kernel_cycles;
  /* Process listing (abi/process_view.h). The key is fixed at creation; the
   * label and launcher during preparation; the rest at publication. */
  uint64_t list_key, launched_by, group_key, start_ns;
  char label[PROCESS_LIST_LABEL_MAX];
  /* The space's execution tree guard: published and not yet retired. */
  struct process *space_next;
  bool listed;
  struct shared_origin *shared_origin; /* Owned; no back-reference to this process. */
  struct shared_process *shared_views; /* Private VM owner; exit joins alias removal. */
  size_t shared_view_bytes, shared_view_count, shared_view_metadata_bytes;
};

/* Borrow the executing user task's process; NULL for scheduler/kernel tasks.
 * Scheduler must be initialized on this CPU. IF=0, kernel GS active, outside
 * interrupt/fault entry. The pointer cannot outlive this execution context. */
struct process *process_current(void);

/* Retained kernel-root preparation, IF=0, after heap/space initialization.
 * The caller exclusively owns
 * an inactive private address space. Success transfers it to the new process;
 * failure leaves it with the caller and clears *result when non-NULL. */
enum mm_result process_create(struct space *space, struct vm_space *address_space,
                              struct process **result);
/* Retained current user in kernel root, IF=0. Lazily capture the immutable
 * origin charge owner; returns an owned metadata reference on success. */
enum call_status process_shared_origin(struct process *process, struct shared_origin **result);

/* IF=0, retained kernel-root actor exclusively owns this unpublished process. */
void process_prepare_require(struct process *process);
/* Transfer exclusive unpublished ownership to its preparation CPU RAM actor. */
void process_prepare_transfer(struct process *process);

/* Exclusive unpublished owner, IF=0, no task storage/receivers/sessions.
 * Reclaims private backing in bounded steps, then allocation/capability/ENV
 * records; may yield under retained kernel-root ownership. No process result
 * is published. Logical handle closes run normally; fixed destructors keep
 * their owners and group cleanup attribution. */
enum mm_result process_destroy(struct process *process);

/* Retained caller, IF=0, exclusive preparation. Exactly one task may be prepared, including
 * a blocked or not-yet-published task. Failure leaves both objects with caller. */
bool process_task_available(struct process *process);
void process_task_attach(struct process *process, struct process_task_member *member,
                         struct task *task);
void process_task_discard(struct process *process, struct process_task_member *member);
/* Exclusive preparation, before ready-queue publication. Transfers process lifetime to finalization. */
void process_publish(struct process *process);
/* Preparation: the listed label is PATH's last component, truncated. */
void process_set_label(struct process *process, const char *path);
/* A per-boot listing key, never reused; processes and groups share the series. */
uint64_t process_list_key_next(void);
/* Space execution tree guard held, IF=0: fills RECORD's process fields from
 * PROCESS, taking its lifetime lock. */
void process_list_sample(struct process *process, struct process_list_record *record);

/* Caller CPU, IF=0, lifetime_lock held. Adds a sibling only while admission is open;
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

/* Permanent scheduler departure records the actual CPU before queueing. The
 * BSP detaches the stop target, then that CPU's fixed RAM actor reclaims storage
 * and context pins before native thread completion and reclaimed(). The last detach elects EXITED/0
 * only when no process-wide event exists and closes VM admission. For the last
 * storage, reclaimed returns true and queues the process for the reaper only
 * when no cleanup pins remain; the caller must not access it afterwards. The
 * final task storage or cleanup pin marks RETIRED and detaches control/group
 * membership before queueing without allocation. The reaper releases
 * the address space in bounded interrupts-off steps, then the remaining
 * resources, and only then publishes control and group completion. */
/* Permanent scheduler stack, after final root/stack departure, before enqueue. */
void process_task_depart(struct process *process, size_t departure_cpu);
/* Also adds the departing task's CYCLES to the process totals. */
void process_task_detach(struct process *process, struct process_task_member *member,
    uint64_t user_cycles, uint64_t kernel_cycles);
bool process_task_reclaimed(struct process *process);
/* IF=0, caller already keeps the submitted process live. Retain borrowed
 * process records through whole-record cleanup after the syscall returns.
 * Pin and unpin are any CPU while task storage remains. The final unpin after
 * all task storage is gone uses the recorded last departure CPU for RAM cleanup. Process,
 * control and group completion wait for both task storage and these pins.
 * No access to process is permitted after unpin without another live owner. */
bool process_cleanup_pin(struct process *process);
void process_cleanup_unpin(struct process *process);
/* Boot BSP, IF=0, after task_init(). Creates all mandatory fixed RAM actors
 * and the coordinator for unchanged BSP device hooks before user admission. */
void process_reaper_init(void);
/* Any CPU, IF=0, after releasing the producer's retirement queue guard. Wake
 * the existing reaper for retained cleanup; no allocation or context switch. */
void process_cleanup_notify(void);
/* Wake the fixed RAM actor assigned to an already-owned retirement record. */
void process_ram_cleanup_notify(size_t cpu);
/* Display retirement transfers an owned process cleanup record back to its RAM
 * actor only after all unleased display backing has actually been reclaimed. */
void process_display_cleanup_ready(struct process *process);

#endif
