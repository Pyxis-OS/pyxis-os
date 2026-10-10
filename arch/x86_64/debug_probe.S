#include <arch/descriptors.h>

.section .text.debug,"ax",@progbits
.macro DEBUG_LOAD name, width, load, store
.global \name
.type \name, @function
\name:
  mov %rsp, (%rdx)
.global \name\()_unsafe
\name\()_unsafe:
  .if \width == 32
    \load (%rdi), %eax
  .else
    \load (%rdi), %rax
  .endif
  \store
  mov $1, %eax
  ret
.size \name, . - \name
.endm

/* Each instruction issues one access of the requested bus width. */
DEBUG_LOAD arch_debug_load8, 8, movzbq, "mov %al, (%rsi)"
DEBUG_LOAD arch_debug_load16, 16, movzwq, "mov %ax, (%rsi)"
DEBUG_LOAD arch_debug_load32, 32, movl, "mov %eax, (%rsi)"
DEBUG_LOAD arch_debug_load64, 64, movq, "mov %rax, (%rsi)"
arch_debug_load_error:
  xor %eax, %eax
  ret

.macro DEBUG_FAULT name, vector
.global \name
.type \name, @function
\name:
  pushq $\vector
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
  call arch_debug_fixup
  test %al, %al
  jz .Lordinary_\name
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
  /* IRET would unblock NMI before the outer NMI releases its IST stack.
   * This exact same-privilege failed load returns through its saved call RSP.
   * SS/CS are unchanged. POPFQ clears RF; retrying the faulting load is avoided. */
  pushq 32(%rsp)
  popfq
  mov 40(%rsp), %rsp
  jmp arch_debug_load_error
.Lordinary_\name:
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
  lea 8(%rsp), %rsp
  jmp *isr_table + \vector * 8(%rip)
.size \name, . - \name
.endm

DEBUG_FAULT arch_debug_page_fault_entry, EXCEPTION_PAGE_FAULT
DEBUG_FAULT arch_debug_general_protection_entry, EXCEPTION_GENERAL_PROTECTION
.section .note.GNU-stack,"",@progbits
