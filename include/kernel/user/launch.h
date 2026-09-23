#ifndef KERNEL_USER_LAUNCH_H
#define KERNEL_USER_LAUNCH_H

#include <kernel/mm/types.h>

struct process;
struct space;

#define USER_INITIAL_STACK_BASE UINT64_C(0x800000)
#define USER_INITIAL_STACK_SIZE PAGE_SIZE

/* BSP, IF=0. Stable kernel image bytes; creates an owned, inactive process and
 * its initial stack. No startup/grants or task submission. Clears outputs and
 * unwinds on failure. Shared by boot and capability-authorized launch. */
enum mm_result user_process_load(struct space *space, const void *bytes, size_t size,
                                  struct process **process, uintptr_t *entry);

/* BSP, IF=0, before scheduler startup. Loads the shell, grants resources
 * and submits it on CPU 1 (or the BSP alone). Failure unwinds the process. */
void user_launch_initial(void);

#endif
