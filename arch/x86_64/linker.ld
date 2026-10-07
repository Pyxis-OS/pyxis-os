OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
KERNEL_LINK_BASE = 0xffffffff80000000;
KERNEL_PAGE_SIZE = 4096;

/* ELF segment flags: PF_X = 1, PF_W = 2, PF_R = 4. LLD evaluates FLAGS()
 * before symbol assignments, so the values are written as numbers. */
PHDRS {
  text PT_LOAD FLAGS(5);   /* PF_R | PF_X */
  rodata PT_LOAD FLAGS(4); /* PF_R */
  data PT_LOAD FLAGS(6);   /* PF_R | PF_W */
}
SECTIONS {
  . = KERNEL_LINK_BASE;
  __kernel_start = .;
  __text_start = .;
  .text : { *(.text .text.*) } :text
  /* Permission boundaries must also be leaf-page boundaries after takeover. */
  . = ALIGN(KERNEL_PAGE_SIZE);
  __text_end = .;
  __rodata_start = .;
  .rodata : { *(.rodata .rodata.*) } :rodata
  . = ALIGN(KERNEL_PAGE_SIZE);
  __rodata_end = .;
  __data_start = .;
  .limine_requests : {
    /* Limine scans between these delimiters, including otherwise unused data. */
    KEEP(*(.limine_requests_start))
    KEEP(*(.limine_requests))
    KEEP(*(.limine_requests_end))
  } :data
  .data : { *(.data .data.*) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data
  . = ALIGN(KERNEL_PAGE_SIZE);
  __data_end = .;
  __kernel_end = .;
  /DISCARD/ : { *(.eh_frame*) *(.note .note.*) *(.comment) }
}
