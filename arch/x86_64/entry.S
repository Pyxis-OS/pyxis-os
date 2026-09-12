.set STACK_ALIGNMENT, 16
.set BOOTSTRAP_STACK_BYTES, 64 * 1024
.set STACK_PAGE_ALIGNMENT, 4096

.section .text
.global _start
.type _start, @function
_start:
  cli
  cld
  /* Leave the bootloader stack before C; SysV requires alignment before CALL. */
  lea bootstrap_stack_top(%rip), %rsp
  and $-STACK_ALIGNMENT, %rsp
  /* Terminate frame-pointer backtraces at the assembly entry. */
  xor %ebp, %ebp
  call limine_entry
1:
  cli
  hlt
  jmp 1b
.size _start, . - _start

.section .bss
.balign STACK_PAGE_ALIGNMENT
bootstrap_stack:
  .skip BOOTSTRAP_STACK_BYTES
.global bootstrap_stack_top
bootstrap_stack_top:
.section .note.GNU-stack,"",@progbits
