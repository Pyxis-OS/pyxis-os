#include <arch/layout.h>
#include <arch/syscall.h>
#include <arch/cpu_local.h>

.set STACK_ALIGNMENT, 16
.set STACK_WORD_BYTES, 8
.set USER_ADDRESS_SHIFT, VIRTUAL_ADDRESS_BITS - 1

.section .text
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
  /* SYSCALL leaves RSP pointing into user memory and does not use TSS.RSP0.
   * Save it without a push or a scratch GPR. IF is already masked by FMASK;
   * GS selects this CPU's entry storage after SWAPGS. */
  swapgs
  mov %rsp, %gs:CPU_USER_RSP_OFFSET
  mov %gs:CPU_SYSCALL_STACK_OFFSET, %rsp

  pushq %gs:CPU_USER_RSP_OFFSET
  push %r11
  push %rcx

  push %rbx
  push %rbp
  push %rdi
  push %rsi
  push %rdx
  push %r8
  push %r9
  push %r10
  push %r12
  push %r13
  push %r14
  push %r15

  /* Sixteen pushes keep the C call site 16-byte aligned. The seventh C
   * parameter (user arg6) goes on the stack; shift the others into SysV order. */
  push %r9
  mov %r8, %r9
  mov %r10, %r8
  mov %rdx, %rcx
  mov %rsi, %rdx
  mov %rdi, %rsi
  mov %rax, %rdi
  call syscall_dispatch
  add $STACK_WORD_BYTES, %rsp

  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %r10
  pop %r9
  pop %r8
  /* Keep the C result's second word in RDX: CALL/CLOSE return reply bytes;
   * log output and unknown calls return the original user RDX. */
  add $STACK_WORD_BYTES, %rsp
  pop %rsi
  pop %rdi
  pop %rbp
  pop %rbx
  pop %rcx

  /* Only user RFLAGS and RSP remain on the kernel stack. Validate before
   * restoring RSP: a noncanonical SYSRET target can fault at CPL0. Requiring
   * the lower half also prevents a user return into shared kernel mappings. */
  mov %rcx, %r11
  shr $USER_ADDRESS_SHIFT, %r11
  jnz .Lbad_return
  mov STACK_WORD_BYTES(%rsp), %r11
  shr $USER_ADDRESS_SHIFT, %r11
  jnz .Lbad_return

  pop %r11
  and $SYSCALL_RETURN_FLAGS_MASK, %r11
  or $(RFLAGS_FIXED | RFLAGS_INTERRUPT_ENABLE), %r11
  /* No stack access after this pop; execution resumes in the user space. */
  pop %rsp
  swapgs
  sysretq

.Lbad_return:
  /* The two remaining words leave RSP aligned for this fatal C call. */
  mov %rcx, %rdi
  mov STACK_WORD_BYTES(%rsp), %rsi
  call arch_bad_user_return
.size syscall_entry, . - syscall_entry

.section .note.GNU-stack,"",@progbits
