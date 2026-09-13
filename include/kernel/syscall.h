#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>

/* Called on the kernel syscall stack with interrupts disabled. Arguments are
 * untrusted user values; a pointer argument must not be blindly dereferenced.
 * Dispatch must return without enabling interrupts, changing address spaces,
 * or issuing another SYSCALL. The initial stub returns -1 for every number. */
int64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6);

#endif
