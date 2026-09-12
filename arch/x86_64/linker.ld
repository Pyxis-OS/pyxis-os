OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
PF_EXECUTE = 1;
PF_WRITE = 2;
PF_READ = 4;
KERNEL_LINK_BASE = 0xffffffff80000000;
KERNEL_PAGE_SIZE = 4096;

PHDRS {
  text PT_LOAD FLAGS(PF_READ | PF_EXECUTE);
  rodata PT_LOAD FLAGS(PF_READ);
  data PT_LOAD FLAGS(PF_READ | PF_WRITE);
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
