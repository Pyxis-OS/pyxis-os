.set LIMINE_MP_EXTRA_ARGUMENT_OFFSET, 24

.section .text
.global limine_ap_entry
.type limine_ap_entry, @function
limine_ap_entry:
  /* This is the AP's last access to Limine data, through its boot mapping. */
  mov LIMINE_MP_EXTRA_ARGUMENT_OFFSET(%rdi), %rdi
  jmp arch_ap_entry
.size limine_ap_entry, . - limine_ap_entry

.section .note.GNU-stack,"",@progbits
