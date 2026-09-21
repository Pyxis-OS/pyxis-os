#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <kernel/mm/types.h>

struct task_wait;

/* Current user task, IF=0. Prepare its wait record before publishing. The
 * record lives in task metadata, whose heap mapping survives stack reuse.
 * Sleep only after releasing resource locks; an earlier wake is remembered.
 * A resource must remove its wait pointer before waking and never use it after
 * wake returns. No allocation or migration; one wait per task at a time. */
struct task_wait *task_wait_prepare(void);
void task_wait_sleep(struct task_wait *wait);
/* Any CPU, IF=0, after detaching the record under its resource lock. */
void task_wait_wake(struct task_wait *wait);

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

/* Current BSP kernel task only, IF=1. Sleep for delivered local timer ticks;
 * zero ticks yields to the end of the ready queue. Resumes with IF=1. */
void kernel_task_sleep(uint64_t ticks);

/* Round-robin queue per CPU. The boot stack becomes the scheduler/cleanup
 * stack. Call once per CPU with IF=0; the BSP releases waiting AP schedulers.
 * Only kernel task bodies and userspace run with interrupts enabled. */
[[noreturn]] void task_schedule(void);

/* Timer entry after EOI, IF=0. user_mode describes the interrupted CS.
 * May switch stacks; never allocates, cleans up or logs in interrupt entry. */
void task_preempt(bool user_mode);

#endif
