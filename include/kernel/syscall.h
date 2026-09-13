#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>

#define SYSCALL_EXIT UINT64_C(1)

/* Called on the kernel syscall stack with interrupts disabled. Arguments are
 * untrusted user values; a pointer argument must not be blindly dereferenced.
 * Ordinary dispatch returns without enabling interrupts, changing address
 * spaces, or issuing another SYSCALL. Exit instead resumes the kernel caller
 * and never returns here. Its status is the signed low 32 bits of arg1.
 * Unknown syscall numbers return -1. */
int64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6);

#endif
