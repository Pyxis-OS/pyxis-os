#ifndef KERNEL_USER_H
#define KERNEL_USER_H

#include <stdint.h>

struct vm_space;

/* Run one loaded image until it exits. The caller owns the space and supplies
 * a valid executable entry and a mapped user stack with a 16-byte-aligned top.
 * Returns the exit status on the original kernel stack, with the kernel space
 * active. The caller can then destroy the inactive user space.
 * Requires initialized VM/heap, one CPU, IF=0, and no other active user run. */
int user_run(struct vm_space *space, uintptr_t entry, uintptr_t stack_top);

/* Called by exit dispatch on the kernel entry stack. Does not return to the
 * dispatcher or free memory; cleanup belongs to the resumed user_run caller. */
[[noreturn]] void user_exit(int status);

#endif
