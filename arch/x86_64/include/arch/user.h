#ifndef ARCH_USER_H
#define ARCH_USER_H

#include <stdint.h>

/* Shared by SYSCALL entry and TSS.RSP0. It must not overlap the suspended
 * kernel caller's stack. Single CPU, no nested user runs, interrupts disabled. */
extern char user_entry_stack_top[];

/* The caller has activated the user space. Entry must be a mapped executable
 * lower-half address; stack_top must be canonical, 16-byte aligned, and above
 * a writable user stack. Returns once, when arch_return_from_user is called.
 * Neither helper changes CR3. The kernel stack must remain mapped throughout. */
int arch_run_user(uintptr_t entry, uintptr_t stack_top);

/* Only from a kernel entry for the active run, with IF and DF clear. Abandons
 * the entry stack and restores the suspended C call, returning its status. */
[[noreturn]] void arch_return_from_user(int status);

#endif
