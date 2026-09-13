#include <arch/layout.h>
#include <arch/syscall.h>

.set STACK_ALIGNMENT, 16
.set STACK_WORD_BYTES, 8
.set USER_ENTRY_STACK_BYTES, 16 * 1024
.set USER_ADDRESS_SHIFT, VIRTUAL_ADDRESS_BITS - 1

.section .text
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
  /* SYSCALL leaves RSP pointing into user memory and does not use TSS.RSP0.
   * Save it without a push or a scratch GPR. IF is already masked by FMASK;
   * one CPU and non-reentrant entry make this private slot sufficient. */
  mov %rsp, saved_user_rsp(%rip)
  lea user_entry_stack_top(%rip), %rsp

  pushq saved_user_rsp(%rip)
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
  pop %rdx
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
  or $RFLAGS_FIXED, %r11
  /* No stack access after this pop; execution resumes in the user space. */
  pop %rsp
  sysretq

.Lbad_return:
  /* The two remaining words leave RSP aligned for this fatal C call. */
  mov %rcx, %rsi
  mov STACK_WORD_BYTES(%rsp), %rdx
  lea bad_return_message(%rip), %rdi
  xor %eax, %eax
  call panic
.size syscall_entry, . - syscall_entry

.section .rodata
bad_return_message:
  .asciz "invalid syscall return rip=0x%lx rsp=0x%lx"

.section .bss
.balign STACK_ALIGNMENT
.type user_entry_stack, @object
user_entry_stack:
  .skip USER_ENTRY_STACK_BYTES
.global user_entry_stack_top
user_entry_stack_top:
.size user_entry_stack, . - user_entry_stack

.balign STACK_WORD_BYTES
.type saved_user_rsp, @object
saved_user_rsp:
  .skip STACK_WORD_BYTES
.size saved_user_rsp, . - saved_user_rsp

.section .note.GNU-stack,"",@progbits
