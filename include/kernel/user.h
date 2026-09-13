#ifndef KERNEL_USER_H
#define KERNEL_USER_H

#include <kernel/mm/types.h>

struct vm_space;

/* BSP only, after boot_start_cpus(), VM and heap initialization. */
void user_init(void);

/* Queue a user task with a private kernel stack. On success, transfers sole
 * ownership of the inactive space to the task; failure leaves it with the
 * caller. Entry must be executable and the writable user stack top aligned
 * to 16 bytes. One task owns one space for now; no sharing/refcounts.
 * Requires user_init(), BSP execution, IF=0, outside interrupt/fault entry.
 * This form pins the task to CPU zero (the BSP). */
enum mm_result user_task_create(struct vm_space *space, uintptr_t entry,
                                uintptr_t stack_top);

/* Same ownership contract, pinned to a dense index below arch_cpu_count().
 * No migration. The caller must not access the space after success, even if
 * the task has not run yet. May submit while other CPUs are scheduling. */
enum mm_result user_task_create_on(size_t cpu_index, struct vm_space *space,
                                   uintptr_t entry, uintptr_t stack_top);

/* Runs the ready queue round-robin and idles with interrupts enabled when
 * empty. The calling kernel stack becomes the scheduler/cleanup stack.
 * The BSP call releases APs waiting here after startup. Completed tasks return
 * to the BSP for reclamation; APs never enter the allocators. Kernel execution
 * otherwise stays at IF=0, including syscalls. Call once per CPU. */
[[noreturn]] void user_schedule(void);

/* Timer entry from userspace only, after acknowledging the interrupt. Saves
 * the current task and may resume another; no allocation, cleanup or logging. */
void user_preempt(void);

/* Abandon the current task's kernel-entry stack. The scheduler switches to
 * its own stack and the kernel space before returning ownership to the BSP. */
[[noreturn]] void user_exit(int status);
[[noreturn]] void user_fault(void);

#endif
