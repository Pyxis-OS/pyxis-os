#ifndef ARCH_DESCRIPTORS_H
#define ARCH_DESCRIPTORS_H

#define GDT_ENTRY_BYTES 8

#define GDT_KERNEL_CODE_INDEX 1
#define GDT_KERNEL_DATA_INDEX 2
/* SYSRETQ selects user CS one GDT entry after user SS. */
#define GDT_USER_DATA_INDEX 3
#define GDT_USER_CODE_INDEX 4
#define GDT_TSS_INDEX 5

#define SELECTOR_RPL_USER 3
#define SELECTOR_RPL_MASK 3

#define GDT_KERNEL_CODE_SELECTOR (GDT_KERNEL_CODE_INDEX * GDT_ENTRY_BYTES)
#define GDT_KERNEL_DATA_SELECTOR (GDT_KERNEL_DATA_INDEX * GDT_ENTRY_BYTES)

#define GDT_USER_CODE_SELECTOR \
  ((GDT_USER_CODE_INDEX * GDT_ENTRY_BYTES) | SELECTOR_RPL_USER)
#define GDT_USER_DATA_SELECTOR \
  ((GDT_USER_DATA_INDEX * GDT_ENTRY_BYTES) | SELECTOR_RPL_USER)

#define GDT_TSS_SELECTOR (GDT_TSS_INDEX * GDT_ENTRY_BYTES)
#define DOUBLE_FAULT_IST 1

#define IDT_VECTOR_COUNT 256
#define EXCEPTION_VECTOR_COUNT 32
#define EXCEPTION_NMI 2
#define EXCEPTION_DOUBLE_FAULT 8
#define EXCEPTION_INVALID_TSS 10
#define EXCEPTION_SEGMENT_NOT_PRESENT 11
#define EXCEPTION_STACK_FAULT 12
#define EXCEPTION_GENERAL_PROTECTION 13
#define EXCEPTION_PAGE_FAULT 14
#define EXCEPTION_MACHINE_CHECK 18
#define EXCEPTION_ALIGNMENT_CHECK 17
#define EXCEPTION_CONTROL_PROTECTION 21
#define EXCEPTION_VMM_COMMUNICATION 29
#define EXCEPTION_SECURITY 30

#ifndef __ASSEMBLER__
#include <stdint.h>

struct descriptor_table_pointer {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));

void gdt_init(void);
void gdt_set_kernel_stack(uintptr_t stack_top);
void idt_init(void);

/* Order matches isr_common's pushes, followed by the vector/error pair and
 * the long-mode CPU frame. RSP and SS are saved even without a ring change. */
struct exception_frame {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
  uint64_t vector, error, rip, cs, rflags, rsp, ss;
};
[[noreturn]] void exception_handler(const struct exception_frame *frame);
void interrupt_handler(struct exception_frame *frame);
#endif
#endif
