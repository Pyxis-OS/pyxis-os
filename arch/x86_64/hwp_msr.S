.text
.global arch_hwp_read_msr
.type arch_hwp_read_msr, @function
arch_hwp_read_msr:
  movl %edi, %ecx
.global arch_hwp_read_site
arch_hwp_read_site:
  rdmsr
  shlq $32, %rdx
  orq %rdx, %rax
  movq %rax, (%rsi)
  movl $1, %eax
  ret
.global arch_hwp_read_failed
arch_hwp_read_failed:
  xorl %eax, %eax
  ret
.size arch_hwp_read_msr, .-arch_hwp_read_msr

.global arch_hwp_write_msr
.type arch_hwp_write_msr, @function
arch_hwp_write_msr:
  movl %edi, %ecx
  movl %esi, %eax
  movq %rsi, %rdx
  shrq $32, %rdx
.global arch_hwp_write_site
arch_hwp_write_site:
  wrmsr
  movl $1, %eax
  ret
.global arch_hwp_write_failed
arch_hwp_write_failed:
  xorl %eax, %eax
  ret
.size arch_hwp_write_msr, .-arch_hwp_write_msr
.section .note.GNU-stack,"",@progbits
