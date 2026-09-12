.section .text
.altmacro
.macro ISR number
.type isr_\number, @function
isr_\number:
  /* CPU supplies an error code for these exceptions, including #CP/#VC/#SX. */
  .if !(\number == 8 || \number == 10 || \number == 11 || \number == 12 || \number == 13 || \number == 14 || \number == 17 || \number == 21 || \number == 29 || \number == 30)
    pushq $0
  .endif
  pushq $\number
  jmp isr_common
.size isr_\number, . - isr_\number
.endm
.set vector, 0
.rept 256
  ISR %vector
  .set vector, vector + 1
.endr

isr_common:
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
  /* The interrupted RSP need not satisfy the SysV call-site alignment. */
  and $-16, %rsp
  call exception_handler
1:
  cli
  hlt
  jmp 1b

.section .rodata
.balign 8
.global isr_table
isr_table:
.macro ISR_POINTER number
  .quad isr_\number
.endm
.set vector, 0
.rept 256
  ISR_POINTER %vector
  .set vector, vector + 1
.endr
.section .note.GNU-stack,"",@progbits
