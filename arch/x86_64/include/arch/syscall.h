#ifndef ARCH_SYSCALL_H
#define ARCH_SYSCALL_H

#define RFLAGS_CARRY (1 << 0)
#define RFLAGS_FIXED (1 << 1)
#define RFLAGS_PARITY (1 << 2)
#define RFLAGS_AUX_CARRY (1 << 4)
#define RFLAGS_ZERO (1 << 6)
#define RFLAGS_SIGN (1 << 7)
#define RFLAGS_TRAP (1 << 8)
#define RFLAGS_INTERRUPT_ENABLE (1 << 9)
#define RFLAGS_DIRECTION (1 << 10)
#define RFLAGS_OVERFLOW (1 << 11)
#define RFLAGS_NESTED_TASK (1 << 14)
#define RFLAGS_ALIGNMENT_CHECK (1 << 18)

/* Entry must not inherit user tracing, string direction, or alignment checks.
 * Syscalls stay non-preemptible; return to userspace explicitly enables IF. */
#define SYSCALL_ENTRY_FLAGS_MASK \
  (RFLAGS_TRAP | RFLAGS_INTERRUPT_ENABLE | RFLAGS_DIRECTION | \
   RFLAGS_NESTED_TASK | RFLAGS_ALIGNMENT_CHECK)

/* Preserve arithmetic flags and user DF. Return forces IF=1 and IOPL=0. */
#define SYSCALL_RETURN_FLAGS_MASK \
  (RFLAGS_CARRY | RFLAGS_PARITY | RFLAGS_AUX_CARRY | RFLAGS_ZERO | \
   RFLAGS_SIGN | RFLAGS_DIRECTION | RFLAGS_OVERFLOW)

#ifndef __ASSEMBLER__
void arch_syscall_init(void);
/* User ABI: RAX = number; RDI, RSI, RDX, R10, R8, R9 = arguments.
 * RAX returns the result. RCX/R11 are clobbered; other GPRs are preserved.
 * Workloads remain BSP-only and non-reentrant; kernel code must not execute SYSCALL. */
void syscall_entry(void);
#endif

#endif
