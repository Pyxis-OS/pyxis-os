#ifndef ARCH_LAYOUT_H
#define ARCH_LAYOUT_H

#define VIRTUAL_ADDRESS_BITS 48

#ifndef __ASSEMBLER__
#include <stdint.h>

#define ARCH_PAGE_SIZE UINT64_C(4096)
#define LOWER_HALF_MAX ((UINT64_C(1) << (VIRTUAL_ADDRESS_BITS - 1)) - 1)
#define HIGHER_HALF_BASE (~LOWER_HALF_MAX)
#define KERNEL_BASE UINT64_C(0xffffffff80000000)
/* PML4 256: virtual allocations; 509: metadata, scratch, framebuffer; 510: recursive. */
#define KERNEL_VM_BASE HIGHER_HALF_BASE
#define KERNEL_VM_SIZE (UINT64_C(64) << 30)
#define PMM_METADATA_BASE UINT64_C(0xfffffe8000000000)
#define TEMP_MAP_BASE UINT64_C(0xfffffe8040000000)
#define APIC_BASE UINT64_C(0xfffffe8040200000)
#define IO_APIC_BASE (APIC_BASE + ARCH_PAGE_SIZE)
#define FRAMEBUFFER_BASE UINT64_C(0xfffffe8080000000)
#define FRAMEBUFFER_END UINT64_C(0xffffff0000000000) /* Start of recursive slot. */
#define RECURSIVE_SLOT 510

extern char __kernel_start[], __kernel_end[];
extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __data_end[];
#endif
#endif
