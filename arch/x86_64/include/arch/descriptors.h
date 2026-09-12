#ifndef ARCH_DESCRIPTORS_H
#define ARCH_DESCRIPTORS_H

#define GDT_ENTRY_BYTES 8
#define GDT_KERNEL_CODE_INDEX 1
#define GDT_KERNEL_DATA_INDEX 2
#define GDT_TSS_INDEX 3
/* A selector contains index[15:3], table choice[2] (GDT=0), and requested
 * privilege[1:0]. Its RPL is distinct from the descriptor's DPL. */
#define SELECTOR_TABLE_GDT 0
#define SELECTOR_RPL_KERNEL 0
#define GDT_KERNEL_CODE_SELECTOR \
  (GDT_KERNEL_CODE_INDEX * GDT_ENTRY_BYTES | SELECTOR_TABLE_GDT | SELECTOR_RPL_KERNEL)
#define GDT_KERNEL_DATA_SELECTOR \
  (GDT_KERNEL_DATA_INDEX * GDT_ENTRY_BYTES | SELECTOR_TABLE_GDT | SELECTOR_RPL_KERNEL)
#define GDT_TSS_SELECTOR \
  (GDT_TSS_INDEX * GDT_ENTRY_BYTES | SELECTOR_TABLE_GDT | SELECTOR_RPL_KERNEL)
#define DOUBLE_FAULT_IST 1

#define IDT_VECTOR_COUNT 256
#define EXCEPTION_DOUBLE_FAULT 8
#define EXCEPTION_INVALID_TSS 10
#define EXCEPTION_SEGMENT_NOT_PRESENT 11
#define EXCEPTION_STACK_FAULT 12
#define EXCEPTION_GENERAL_PROTECTION 13
#define EXCEPTION_PAGE_FAULT 14
#define EXCEPTION_ALIGNMENT_CHECK 17
#define EXCEPTION_CONTROL_PROTECTION 21
#define EXCEPTION_VMM_COMMUNICATION 29
#define EXCEPTION_SECURITY 30

#ifndef __ASSEMBLER__
#include <stdint.h>

struct descriptor_table_pointer {
  uint16_t limit; /* Table byte size minus one, not an entry count. */
  uint64_t base;
} __attribute__((packed));

void gdt_init(void);
void idt_init(void);

/* Order matches isr_common's pushes, followed by the vector/error pair and
 * the long-mode CPU frame. RSP and SS are saved even without a ring change. */
struct exception_frame {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
  uint64_t vector, error, rip, cs, rflags, rsp, ss;
};
[[noreturn]] void exception_handler(const struct exception_frame *frame);
#endif
#endif
