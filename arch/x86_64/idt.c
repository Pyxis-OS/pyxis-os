#include <arch/cpu.h>
#include <arch/descriptors.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <stddef.h>

struct idt_gate {
  uint16_t offset_low, selector;
  uint8_t ist, attributes;
  uint16_t offset_mid;
  uint32_t offset_high, reserved;
} __attribute__((packed));

static struct idt_gate idt[256] __attribute__((aligned(16)));
extern void (*const isr_table[256])(void);

void idt_init(void)
{
  _Static_assert(sizeof(struct idt_gate) == 16, "IDT gate hardware layout");
  _Static_assert(offsetof(struct exception_frame, vector) == 15 * 8, "ISR frame");
  for (size_t i = 0; i < 256; ++i) {
    uintptr_t address = (uintptr_t)isr_table[i];
    idt[i] = (struct idt_gate){
      .offset_low = address,
      .selector = 8,
      .ist = i == 8 ? 1 : 0,
      .attributes = 0x8e,
      .offset_mid = address >> 16,
      .offset_high = address >> 32,
    };
  }
  const struct {
    uint16_t limit;
    uint64_t base;
  } __attribute__((packed)) idtr = {sizeof(idt) - 1, (uintptr_t)idt};
  __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

[[noreturn]] void exception_handler(const struct exception_frame *f)
{
  uint64_t fault_address = read_cr2();
  klog("\nCaelum exception: vector=%lu error=0x%lx rip=0x%lx\n",
       f->vector, f->error, f->rip);
  klog("cs=0x%lx flags=0x%lx rsp=0x%lx ss=0x%lx rbp=0x%lx\n",
       f->cs, f->rflags, f->rsp, f->ss, f->rbp);
  klog("rax=0x%lx rbx=0x%lx rcx=0x%lx rdx=0x%lx rsi=0x%lx rdi=0x%lx\n",
       f->rax, f->rbx, f->rcx, f->rdx, f->rsi, f->rdi);
  klog("r8=0x%lx r9=0x%lx r10=0x%lx r11=0x%lx\n", f->r8, f->r9, f->r10, f->r11);
  klog("r12=0x%lx r13=0x%lx r14=0x%lx r15=0x%lx cr3=0x%lx\n",
       f->r12, f->r13, f->r14, f->r15, read_cr3());
  if (f->vector == 14) {
    klog("page fault: cr2=0x%lx %s %s %s reserved=%u fetch=%u pkey=%u shadow=%u\n",
         fault_address, f->error & 1 ? "protection" : "not-present",
         f->error & 2 ? "write" : "read", f->error & 4 ? "user" : "supervisor",
         (unsigned)((f->error >> 3) & 1), (unsigned)((f->error >> 4) & 1),
         (unsigned)((f->error >> 5) & 1), (unsigned)((f->error >> 6) & 1));
  }
  panic("fatal exception");
}
