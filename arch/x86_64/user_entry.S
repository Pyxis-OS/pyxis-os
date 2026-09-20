#include <arch/descriptors.h>
#include <arch/syscall.h>

.section .text
.global arch_context_switch
.type arch_context_switch, @function
arch_context_switch:
  /* Pair with switch_frame. Preempted registers are already on the IRQ stack. */
  push %rbx
  push %rbp
  push %r12
  push %r13
  push %r14
  push %r15
  mov %rsp, (%rdi)
  mov %rsi, %rsp
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbp
  pop %rbx
  ret
.size arch_context_switch, . - arch_context_switch

.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
  pushq $GDT_USER_DATA_SELECTOR
  push %rsi
  pushq $(RFLAGS_FIXED | RFLAGS_INTERRUPT_ENABLE)
  pushq $GDT_USER_CODE_SELECTOR
  push %rdi

  /* RDX is the startup record's user address, now delivered in RDI. Clear
   * everything else so no kernel register contents escape to userspace. */
  mov %rdx, %rdi
  xor %eax, %eax
  xor %ebx, %ebx
  xor %ecx, %ecx
  xor %edx, %edx
  xor %ebp, %ebp
  xor %esi, %esi
  xor %r8d, %r8d
  xor %r9d, %r9d
  xor %r10d, %r10d
  xor %r11d, %r11d
  xor %r12d, %r12d
  xor %r13d, %r13d
  xor %r14d, %r14d
  xor %r15d, %r15d
  swapgs
  iretq
.size arch_enter_user, . - arch_enter_user

.section .note.GNU-stack,"",@progbits
