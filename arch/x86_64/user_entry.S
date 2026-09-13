#include <arch/syscall.h>

.set STACK_WORD_BYTES, 8

.section .text
.global arch_run_user
.type arch_run_user, @function
arch_run_user:
  /* Preserve the SysV callee-saved registers on the suspended kernel stack.
   * The original CALL already placed our return address above these words. */
  push %rbx
  push %rbp
  push %r12
  push %r13
  push %r14
  push %r15
  mov %rsp, saved_kernel_rsp(%rip)

  mov %rdi, %rcx
  mov $RFLAGS_FIXED, %r11
  mov %rsi, %rsp
  sysretq
.size arch_run_user, . - arch_run_user

.global arch_return_from_user
.type arch_return_from_user, @function
arch_return_from_user:
  /* Exit never unwinds the syscall frame or returns to user code. Shared
   * kernel mappings let us restore this stack before the caller changes CR3. */
  mov %edi, %eax
  mov saved_kernel_rsp(%rip), %rsp
  movq $0, saved_kernel_rsp(%rip)

  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbp
  pop %rbx
  ret
.size arch_return_from_user, . - arch_return_from_user

.section .bss
.balign STACK_WORD_BYTES
.type saved_kernel_rsp, @object
saved_kernel_rsp:
  .skip STACK_WORD_BYTES
.size saved_kernel_rsp, . - saved_kernel_rsp

.section .note.GNU-stack,"",@progbits
