#include <arch/smp.h>

.set STACK_ALIGNMENT, 16

.section .text
.global arch_ap_entry
.type arch_ap_entry, @function
arch_ap_entry:
  cli
  cld
  mov %rdi, %rbx
  and $-STACK_ALIGNMENT, %rsp
  /* Configure NX/WP and clear global/PCID state while the boot stack is valid. */
  call paging_prepare_ap
  mov AP_BOOT_ROOT_OFFSET(%rbx), %rax
  mov AP_BOOT_STACK_OFFSET(%rbx), %rdx
  mov AP_BOOT_CPU_OFFSET(%rbx), %rdi
  /* No stack access between replacing the root and adopting our own stack. */
  mov %rax, %cr3
  mov %rdx, %rsp
  xor %ebp, %ebp
  call arch_ap_main
.size arch_ap_entry, . - arch_ap_entry

.section .note.GNU-stack,"",@progbits
