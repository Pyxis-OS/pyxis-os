#ifndef ARCH_DESCRIPTORS_H
#define ARCH_DESCRIPTORS_H
#include <stdint.h>
void gdt_init(void);
void idt_init(void);
struct exception_frame {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
  uint64_t vector, error, rip, cs, rflags, rsp, ss;
};
[[noreturn]] void exception_handler(const struct exception_frame *frame);
#endif
