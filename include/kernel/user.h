#ifndef KERNEL_USER_H
#define KERNEL_USER_H

#include <kernel/mm/types.h>

struct process;
struct task;

/* Queue one user task with a private kernel stack. On success, transfers the
 * unsubmitted process to its submitted lifetime; failure leaves it with the
 * caller. Entry must be executable and the writable user stack top aligned
 * to 16 bytes in the process's inactive address space. The scheduler places the
 * task on the least-loaded CPU its space allows and may later move it between
 * user-mode boundaries. Requires process_prepare_startup(); its record is passed
 * to the user entry. Requires task_init(), BSP execution, IF=0, outside
 * interrupt/fault entry. The caller must not access the process or its address
 * space after success, even if the task has not run yet. */
enum mm_result user_task_create(struct process *process, uintptr_t entry,
                                uintptr_t stack_top);

/* BSP only. Preparation owns its stack and request/profile storage but borrows
 * the inactive process until publication. PREFERRED_CPU only breaks placement
 * ties; SIZE_MAX expresses none. Failure leaves process ownership with the
 * caller. Discard prepared tasks before destroying their processes. All tasks
 * in a group must be ready before publication, which places each one; afterward
 * neither tasks nor processes may be inspected by the caller. */
enum mm_result user_task_prepare(struct process *process, uintptr_t entry,
                                 uintptr_t stack_top, size_t preferred_cpu,
                                 struct task **result);
void user_task_discard_prepared(struct task *task);
/* Exclusive prepared-task access only, before publication. */
struct process *user_task_process(struct task *task);
void user_task_publish_group(struct task **tasks, size_t count);

/* Abandon the current task's kernel-entry stack. The scheduler switches to
 * its own stack and the kernel space before returning ownership to the BSP. */
[[noreturn]] void user_exit(int status);
[[noreturn]] void user_fault(void);

#endif
