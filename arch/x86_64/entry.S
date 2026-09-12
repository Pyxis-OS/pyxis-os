.section .text
.global _start
.type _start, @function
_start:
  cli
  cld
  lea bootstrap_stack_top(%rip), %rsp
  and $-16, %rsp
  xor %ebp, %ebp
  call limine_entry
1:
  cli
  hlt
  jmp 1b
.size _start, . - _start

.section .bss
.balign 4096
bootstrap_stack:
  .skip 65536
.global bootstrap_stack_top
bootstrap_stack_top:
.section .note.GNU-stack,"",@progbits
