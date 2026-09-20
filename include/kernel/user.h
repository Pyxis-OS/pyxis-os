#ifndef KERNEL_USER_H
#define KERNEL_USER_H

#include <kernel/mm/types.h>

struct process;

/* Queue one user task with a private kernel stack. On success, transfers sole
 * ownership of the unsubmitted process to the task; failure leaves it with the
 * caller. Entry must be executable and the writable user stack top aligned
 * to 16 bytes in the process's inactive address space. The target CPU must
 * host the process's owning space. No sharing, resubmission or migration.
 * Requires task_init(), BSP execution, IF=0, outside interrupt/fault entry.
 * This form pins the task to CPU zero (the BSP). */
enum mm_result user_task_create(struct process *process, uintptr_t entry,
                                uintptr_t stack_top);

/* Same ownership contract, pinned to a dense index below arch_cpu_count().
 * The caller must not access the process or its address space after success,
 * even if the task has not run yet. May submit while other CPUs are scheduling. */
enum mm_result user_task_create_on(size_t cpu_index, struct process *process,
                                   uintptr_t entry, uintptr_t stack_top);

/* Abandon the current task's kernel-entry stack. The scheduler switches to
 * its own stack and the kernel space before returning ownership to the BSP. */
[[noreturn]] void user_exit(int status);
[[noreturn]] void user_fault(void);

#endif
