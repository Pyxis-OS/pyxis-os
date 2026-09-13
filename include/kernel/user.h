#ifndef KERNEL_USER_H
#define KERNEL_USER_H

#include <kernel/mm/types.h>

struct vm_space;

/* Queue a user task with a private kernel stack. On success, transfers sole
 * ownership of the inactive space to the task; failure leaves it with the
 * caller. Entry must be executable and the writable user stack top aligned
 * to 16 bytes. One task owns one space for now; no sharing/refcounts.
 * Requires initialized VM/heap, BSP execution, IF=0, outside interrupt/fault entry. */
enum mm_result user_task_create(struct vm_space *space, uintptr_t entry,
                                uintptr_t stack_top);

/* Runs the ready queue round-robin and idles with interrupts enabled when
 * empty. The calling kernel stack becomes the scheduler/cleanup stack.
 * Kernel execution otherwise stays at IF=0, including syscalls. */
[[noreturn]] void user_schedule(void);

/* Timer entry from userspace only, after acknowledging the interrupt. Saves
 * the current task and may resume another; no allocation, cleanup or logging. */
void user_preempt(void);

/* Abandon the current task's kernel-entry stack. The scheduler switches to
 * its own stack and the kernel space before reclaiming task resources. */
[[noreturn]] void user_exit(int status);
[[noreturn]] void user_fault(void);

#endif
