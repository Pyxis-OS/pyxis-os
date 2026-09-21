#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <abi/syscall.h>

/* Called on the kernel syscall stack with interrupts disabled. Arguments are
 * untrusted user values; a pointer argument must not be blindly dereferenced.
 * Endpoint calls may block on the task's kernel stack. They resume with IF=0
 * on the same CPU and private root before touching user memory. Exit instead
 * abandons the task stack and never returns here; its status is arg1's signed
 * low 32 bits.
 * CALL/CLOSE return status/reply bytes in RAX/RDX. CLOSE has no reply bytes.
 * Legacy calls preserve user RDX by returning arg3 in the second word.
 * Unknown numbers return -1 in RAX. */
struct syscall_result syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6);

#endif
