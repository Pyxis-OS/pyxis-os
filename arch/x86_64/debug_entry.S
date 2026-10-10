#include <arch/descriptors.h>

.section .text.debug,"ax",@progbits
.global arch_debug_nmi_entry
.type arch_debug_nmi_entry, @function
arch_debug_nmi_entry:
  /* IST supplies a kernel stack even in SYSCALL's user-RSP/GS windows.
   * No SWAPGS or GS access: preserve the interrupted orientation exactly. */
  pushq $0
  pushq $EXCEPTION_NMI
  cld
  push %rax
  push %rbx
  push %rcx
  push %rdx
  push %rbp
  push %rsi
  push %rdi
  push %r8
  push %r9
  push %r10
  push %r11
  push %r12
  push %r13
  push %r14
  push %r15
  mov %rsp, %rdi
  mov %rsp, %rbx
  and $-16, %rsp
  call arch_debug_nmi_handler
  mov %rax, %rdi
  call arch_debug_nmi_exit
  mov %rbx, %rsp
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %r11
  pop %r10
  pop %r9
  pop %r8
  pop %rdi
  pop %rsi
  pop %rbp
  pop %rdx
  pop %rcx
  pop %rbx
  pop %rax
  add $16, %rsp
  iretq
.size arch_debug_nmi_entry, . - arch_debug_nmi_entry
.section .note.GNU-stack,"",@progbits
