#include <arch/descriptors.h>

.set STACK_ALIGNMENT, 16
.set INTERRUPT_CS_OFFSET, 24

.section .text
.altmacro
.macro ISR number
.type isr_\number, @function
isr_\number:
  /* CPU supplies an error code for these exceptions, including #CP/#VC/#SX. */
  .if !(\number == EXCEPTION_DOUBLE_FAULT || \
        \number == EXCEPTION_INVALID_TSS || \
        \number == EXCEPTION_SEGMENT_NOT_PRESENT || \
        \number == EXCEPTION_STACK_FAULT || \
        \number == EXCEPTION_GENERAL_PROTECTION || \
        \number == EXCEPTION_PAGE_FAULT || \
        \number == EXCEPTION_ALIGNMENT_CHECK || \
        \number == EXCEPTION_CONTROL_PROTECTION || \
        \number == EXCEPTION_VMM_COMMUNICATION || \
        \number == EXCEPTION_SECURITY)
    pushq $0
  .endif
  pushq $\number
  jmp isr_common
.size isr_\number, . - isr_\number
.endm
.set vector, 0
.rept IDT_VECTOR_COUNT
  ISR %vector
  .set vector, vector + 1
.endr

isr_common:
  /* Kernel entries already have CPU-local GS; user entries still have user GS. */
  testb $SELECTOR_RPL_MASK, INTERRUPT_CS_OFFSET(%rsp)
  jz .Lkernel_entry
  swapgs
.Lkernel_entry:
  /* C assumes forward string operations, regardless of the interrupted DF. */
  cld
  /* Keep this order paired with struct exception_frame. */
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
  /* The interrupted RSP need not satisfy the SysV call-site alignment. */
  and $-STACK_ALIGNMENT, %rsp
  call interrupt_handler
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
  testb $SELECTOR_RPL_MASK, INTERRUPT_CS_OFFSET(%rsp)
  jz .Lkernel_return
  swapgs
.Lkernel_return:
  add $16, %rsp /* Vector and error code precede the CPU's IRET frame. */
  iretq

.section .rodata
.balign 8
.global isr_table
isr_table:
.macro ISR_POINTER number
  .quad isr_\number
.endm
.set vector, 0
.rept IDT_VECTOR_COUNT
  ISR_POINTER %vector
  .set vector, vector + 1
.endr
.section .note.GNU-stack,"",@progbits
