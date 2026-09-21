#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <abi/syscall.h>

/* Called on the kernel syscall stack with interrupts disabled. Arguments are
 * untrusted user values; a pointer argument must not be blindly dereferenced.
 * Ordinary dispatch returns without enabling interrupts, changing address
 * spaces, or issuing another SYSCALL. Exit instead resumes the kernel caller
 * and never returns here. Its status is the signed low 32 bits of arg1.
 * CALL returns status/reply bytes in RAX/RDX. Legacy calls preserve user RDX
 * by returning arg3 in the second word. Unknown numbers return -1 in RAX. */
struct syscall_result syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6);

#endif
