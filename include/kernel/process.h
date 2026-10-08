#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/mm/types.h>
#include <kernel/object/capability.h>

struct space;
struct vm_space;
struct private_allocation;
struct process_control;
struct endpoint;
struct execution_group;

/* One user task per process. Owns address_space, private allocation records,
 * capabilities, a process-control reference and optional execution-group storage. space is borrowed from the initialized set and must outlive
 * the process. */
struct process {
  struct space *space;
  struct vm_space *address_space;
  struct capability_table capabilities;
  struct process_control *control; /* Owned reference; reaper takes it before destruction. */
  struct execution_group *execution_group; /* Owned storage; reaper completes published membership. */
  struct endpoint *endpoints; /* BSP-owned weak list of receiving endpoints. */
  struct private_allocation *allocations; /* Private-memory service regions only. */
  uintptr_t startup_address; /* Read-only record in address_space; zero until prepared. */
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

/* BSP, IF=0, with exclusive ownership of an unsubmitted or retired process.
 * A submitted process belongs to its task until the BSP reaper receives it
 * after leaving the task stack and private root. Success frees the address
 * space, private allocation records, capability table and process. Owned
 * keyboard, graphics and audio sessions are released first; if VM destruction fails,
 * the remaining resources stay intact. In-flight presentation may retain only pixel backing.
 * Audio exit invalidates ownership without allocation; its worker retains queue
 * storage and an attributed cleanup token, never the retired process.
 * Object references are released for BSP reaping; the owning space survives.
 * Releases any remaining control reference without publishing completion; the task reaper takes that reference first and
 * publishes only after also reclaiming the task stack and metadata. Published
 * execution-group membership is likewise transferred to the reaper first;
 * destruction of an unpublished process only releases its group storage. */
enum mm_result process_destroy(struct process *process);

#endif
