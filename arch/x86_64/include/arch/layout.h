#ifndef ARCH_LAYOUT_H
#define ARCH_LAYOUT_H
#include <stdint.h>
#define ARCH_PAGE_SIZE UINT64_C(4096)
#define KERNEL_BASE UINT64_C(0xffffffff80000000)
/* PML4 256: 64 GiB of virtual allocations; 509: metadata and scratch; 510: recursive. */
#define KERNEL_VM_BASE UINT64_C(0xffff800000000000)
#define KERNEL_VM_SIZE UINT64_C(0x1000000000)
#define PMM_METADATA_BASE UINT64_C(0xfffffe8000000000)
#define TEMP_MAP_BASE UINT64_C(0xfffffe8040000000)
#define RECURSIVE_SLOT 510
extern char __kernel_start[], __kernel_end[];
extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __data_end[];
#endif
