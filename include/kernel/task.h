#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <kernel/mm/types.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>

struct profile_snapshot;
struct task_wait;
struct directory_entry;
struct file_object;
struct file_wait;
struct process_wait;
struct console_wait;
struct pipe_wait;
struct pipe_create_reply;
struct memory_region;
struct display_object;
struct display_buffer;
struct launch_capture;
struct launch_group;
struct hostfs_request;

/* Current user task or BSP kernel task, in task context with IF=0. Prepare its
 * wait record before publishing it under the resource lock, after checking the
 * condition under that lock. The record lives in task metadata, whose heap
 * mapping survives stack reuse. Sleep after releasing all locks; an earlier
 * wake is remembered. Sleep returns with IF=0; kernel callers save/disable
 * interrupts before preparation and restore them after detaching the record.
 * A resource must remove its wait pointer before waking and never use it after
 * wake returns. Recheck the condition after wake. Detach any published pointer
 * before reusing the record or returning from a kernel task's entry.
 * No allocation or migration; one wait per task at a time. */
struct task_wait *task_wait_prepare(void);
void task_wait_sleep(struct task_wait *wait);
/* Any CPU, including interrupt entry, IF=0, after detaching the record under
 * its resource lock. Makes a parked task runnable and notifies a remote CPU;
 * never switches to it here. Early wakes only record notification. */
void task_wait_wake(struct task_wait *wait);

/* Absolute monotonic nanosecond deadlines. Relative conversion saturates on
 * overflow. Use one deadline across all waits within an operation. Expiry is
 * checked during BSP scheduling/timer interrupts; wakeup may be late. */
uint64_t task_deadline_after_ms(uint32_t milliseconds);
bool task_deadline_expired(uint64_t deadline);
/* As sleep, but also resumes at deadline. After return, the caller MUST detach
 * any remaining resource pointer under its resource lock before reusing the
 * wait record. A concurrent resource wake remains safe until that detachment.
 * Recheck resource state under the lock; a wake is not a grant of ownership. */
void task_wait_sleep_until(struct task_wait *wait, uint64_t deadline);

/* Current user task, IF=0, no held locks. Lends its capability table to the
 * BSP and blocks until growth completes. No AP allocation or remote stack
 * access. Existing handles/references survive even when allocation fails. */
enum capability_result task_grow_capabilities(void);

/* Current user task, IF=0, no held locks. A focused BSP service allocates or
 * discards an unpublished or removed RAM entry. Requests live in task metadata,
 * never on a remote private stack. Allocation returns NULL on exhaustion.
 * The caller fills the name and publishes the entry or returns it for disposal.
 * Removed entries must have no list links or borrowed readers before disposal. */
struct directory_entry *task_allocate_directory_entry(uint64_t kind, size_t name_length);
/* Name storage only; its child reference remains NULL until rename commits. */
struct directory_entry *task_allocate_directory_name(size_t name_length);
void task_discard_directory_entry(struct directory_entry *entry);

/* Current user task, IF=0. The file queue uses this task-owned record until it
 * detaches and wakes the waiter. One wait per task; no private-stack pointers. */
struct file_wait *task_prepare_file_wait(void);

/* Same lifetime as the file wait record, for completion observers and readers. */
struct process_wait *task_prepare_process_wait(void);
struct console_wait *task_prepare_console_wait(void);
struct pipe_wait *task_prepare_pipe_wait(void);

/* Current user task lends its table to the BSP for atomic pipe creation and
 * installation. Failure installs neither handle. */
enum call_status task_create_pipe(struct pipe_create_reply *reply);

/* Current user task, IF=0, no spinlocks held. Lend exclusive file operation
 * ownership to the BSP to replace/release backing; return it after completion. */
bool task_replace_file_buffer(struct file_object *file, size_t capacity);

/* Current user task, IF=0, no held locks. Copies the checked operation/region
 * into task metadata and blocks. The scheduler publishes only after leaving
 * the private root and task stack; BSP returns ownership through wakeup.
 * region is local caller storage, never dereferenced remotely. */
enum mm_result task_request_memory(uint64_t operation, struct memory_region *region);

/* Current user task, IF=0. Caller validates reply storage before state changes.
 * No allocation or remote inspection; one task per process at present. */
enum call_status task_profile_control(uint64_t operation, struct profile_snapshot *reply);

/* Same inactive-root handoff as private memory. The capability keeps display
 * alive while blocked; reply is copied through task metadata, never remotely. */
enum call_status task_request_display(struct display_object *display,
    uint64_t operation, struct display_buffer *reply);

/* Current user task, IF=0, no spinlocks. Staging allocation/disposal runs on
 * BSP. Launch/discard consumes capture and any owned host image. Launch borrows
 * the caller's table and any in-memory image operation, returning after their
 * release. No remote user/stack access. */
struct launch_capture *task_allocate_launch_capture(void);
void task_discard_launch_capture(struct launch_capture *capture);
enum call_status task_launch_process(struct launch_capture *capture, handle_t *child);
struct launch_group *task_create_launch_group(void);
enum call_status task_prepare_launch_group(struct launch_group *group,
    struct launch_capture *capture);
void task_publish_launch_group(struct launch_group *group, handle_t *children);
void task_discard_launch_group(struct launch_group *group);

/* Current user task, IF=0, no held locks. Fill the prepared shared record,
 * then submit and block. Its capability and private mappings remain live;
 * only this caller copies user memory. Consume the reply before preparing
 * another request. There is no external task cancellation in this model. */
struct hostfs_request *task_prepare_hostfs(void);
void task_submit_hostfs(struct hostfs_request *request);

/* BSP only, after boot_start_cpus(), VM and heap initialization. */
void task_init(void);

/* Private kernel stack, shared kernel address space, pinned to the BSP.
 * Requires task_init(), BSP execution and IF=0, outside interrupt/fault entry.
 * The argument is borrowed and must outlive the task. Returning from entry
 * ends the task; the scheduler frees its stack and metadata.
 * Entry runs with IF=1. Kernel tasks must keep IF=0 around allocator/VM calls
 * and other services that require it; never sleep while holding a lock.
 * As elsewhere in the kernel, FP/SIMD use is forbidden. */
enum mm_result kernel_task_create(void (*entry)(void *), void *argument);

/* Task context, IF=0. Tests the current task's entry without lending task
 * metadata. Used to enforce services owned by one kernel worker. */
bool kernel_task_is_current(void (*entry)(void *));

/* Current BSP kernel task only, IF=1. Sleep until a monotonic nanosecond
 * deadline; a past deadline yields to the ready queue. Resumes with IF=1. */
void kernel_task_sleep_until(uint64_t deadline);

/* Round-robin queue per CPU. The boot stack becomes the scheduler/cleanup
 * stack. Call once per CPU with IF=0; the BSP releases waiting AP schedulers.
 * Only kernel task bodies and userspace run with interrupts enabled. */
[[noreturn]] void task_schedule(void);

/* Timer entry after EOI, IF=0. user_mode describes the interrupted CS.
 * May switch stacks; never allocates, cleans up or logs in interrupt entry. */
void task_preempt(bool user_mode);

#endif
