#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <abi/syscall.h>
#include <kernel/mm/types.h>

struct task_profile;
struct task_wait;
struct task_wait_link;
struct bsp_request;
struct task;
struct execution_group;
struct execution_group_member;
struct space;

/* Current user task only; false for workers and before scheduler setup. A stop
 * request is permanent. Interruptible sleeps resume kernel cleanup, never
 * directly destroy a task or detach its resource registration. False means
 * stop was requested; the caller must detach under its resource lock before
 * reusing the wait or returning. Published loans use uninterruptible sleeps. */
bool task_stop_requested(void);
bool task_wait_stop_requested(const struct task_wait *wait);
bool task_wait_sleep_interruptible(struct task_wait *wait);
bool task_wait_sleep_until_interruptible(struct task_wait *wait, uint64_t deadline);

/* IF=0. The caller keeps the task live by holding either its group lock or
 * the process-control lock guarding a nonnull task link. Marks and wakes an
 * interruptible wait, then notifies the assigned CPU. Does not inspect remote
 * process state. */
void task_request_stop(struct task *task);
/* Inactive task before publication; group owns the link while enrolled. */
struct execution_group_member *task_group_member(struct task *task);

/* Explicit borrowed cleanup context, scoped by object retirement/reaping.
 * Getter falls back to the current user's group. Setter returns the previous
 * explicit context. Separate per-task and scheduler contexts survive switches. */
struct execution_group *task_cleanup_group(void);
struct execution_group *task_cleanup_set_group(struct execution_group *group);

/* Syscall boundaries, IF=0. Marked tasks retire only after their kernel
 * continuation has returned all loans, registrations and owned temporaries. */
void task_syscall_enter(void);
void task_syscall_leave(void);

/* Launch admission from SPACE, IF=0: closes its affinity setup permanently. */
void task_space_close_setup(struct space *space);
/* IF=0. Setup only closes; a true result can go stale only through a launch
 * from the space, and while open the space's sole task is the caller. */
bool task_space_setup_open(struct space *space);
/* Current user task in SPACE, IF=0. Commits an already validated effective CPU
 * set while setup is open, else ENDPOINT_CLOSED. If the caller's CPU is now
 * excluded, it moves at syscall return, before any user instruction runs. */
enum call_status task_space_set_affinity(struct space *space, const uint64_t *cpus);

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
 * serviced by the sleeping CPU's scheduler/timer; execution may be late. */
uint64_t task_deadline_after_ms(uint32_t milliseconds);
bool task_deadline_expired(uint64_t deadline);
/* As sleep, but also resumes at deadline. After return, the caller MUST detach
 * any remaining resource pointer under its resource lock before reusing the
 * wait record. A concurrent resource wake remains safe until that detachment.
 * Recheck resource state under the lock; a wake is not a grant of ownership. */
void task_wait_sleep_until(struct task_wait *wait, uint64_t deadline);

/* Current user task, IF=0. One resource-queue link, separate from BSP request
 * storage. Detach under the resource lock before wake or reuse; timed callers
 * must remove any remaining link after resumption. See kernel/wait.h. */
struct task_wait_link *task_wait_link_prepare(void);

/* Current user task, IF=0. Reserve its preallocated area across all service
 * types until release after result consumption. No submission-time allocation. */
struct bsp_request *task_bsp_request_acquire(void);
struct bsp_request *task_bsp_request_current(void);
void task_bsp_request_release(struct bsp_request *request);
/* Register a prepared deferred request, then sleep using its saved wait record.
 * Only the scheduler publishes it after leaving the private root/task stack. */
void task_bsp_request_defer(struct bsp_request *request);

/* Current user task, IF=0. Persistent caller-only storage; never lend it to a
 * service. Kernel workers have no profiling block. */
struct task_profile *task_profile_current(void);

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

/* Task context, IF=0. Tests the current task's entry and borrowed argument without lending task
 * metadata. Used to enforce services owned by one kernel worker. */
bool kernel_task_is_current(void (*entry)(void *), const void *argument);

/* Current BSP kernel task only, IF=1. Sleep until a monotonic nanosecond
 * deadline; a past deadline yields to the ready queue. Resumes with IF=1. */
void kernel_task_sleep_until(uint64_t deadline);

/* Current BSP kernel task, IF=1, no held locks. Yield to the normal ready queue
 * only when another task is runnable; otherwise return. Resumes with IF=1. */
void kernel_task_yield_if_runnable(void);

/* ACPI worker on the BSP, IF=0. Power-off hold of all user execution: each user
 * task parks at its next return to user mode, whether from a syscall, a timer
 * preemption or the ready queue. Tasks inside syscalls finish them first.
 * Release requeues every parked task. Hold and release alternate. */
void task_user_hold(void);
void task_user_release(void);
/* Round-robin queue per CPU. The boot stack becomes the scheduler/cleanup
 * stack. Call once per CPU with IF=0; the BSP releases waiting AP schedulers.
 * Only kernel task bodies and userspace run with interrupts enabled. */
[[noreturn]] void task_schedule(void);

/* Local timer/reschedule entry or scheduler, IF=0. Expires local deadlines and
 * rearms the CPU's timer before EOI/context switching. Safe before startup;
 * never switches stacks, allocates, cleans up or logs. */
void task_timer_interrupt(void);

/* Timer entry after EOI, IF=0. user_mode describes the interrupted CS.
 * May switch stacks; never allocates, cleans up or logs in interrupt entry. */
void task_preempt(bool user_mode);

#endif
