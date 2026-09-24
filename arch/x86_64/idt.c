#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/descriptors.h>
#include <arch/keyboard.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/user.h>
#include <kernel/task.h>
#include <kernel/virtio/pci.h>
#include <stddef.h>

#define IDT_GATE_PRESENT (1u << 7)
#define IDT_GATE_INTERRUPT64 0xe

#define PAGE_FAULT_PROTECTION (UINT64_C(1) << 0)
#define PAGE_FAULT_WRITE (UINT64_C(1) << 1)
#define PAGE_FAULT_USER (UINT64_C(1) << 2)
#define PAGE_FAULT_RESERVED_BIT (UINT64_C(1) << 3)
#define PAGE_FAULT_INSTRUCTION_FETCH (UINT64_C(1) << 4)
#define PAGE_FAULT_PROTECTION_KEY (UINT64_C(1) << 5)
#define PAGE_FAULT_SHADOW_STACK (UINT64_C(1) << 6)

struct idt_gate {
  uint16_t offset_low, selector;
  uint8_t ist, attributes;
  uint16_t offset_mid;
  uint32_t offset_high, reserved;
} __attribute__((packed));

static struct idt_gate idt[IDT_VECTOR_COUNT] __attribute__((aligned(16)));
extern void (*const isr_table[IDT_VECTOR_COUNT])(void);

void idt_init(void)
{
  _Static_assert(sizeof(struct idt_gate) == 16, "IDT gate hardware layout");
  _Static_assert(offsetof(struct exception_frame, vector) == 15 * sizeof(uint64_t),
                 "ISR frame follows 15 saved general-purpose registers");

  for (size_t vector = 0; vector < IDT_VECTOR_COUNT; ++vector) {
    uintptr_t address = (uintptr_t)isr_table[vector];
    idt[vector] = (struct idt_gate){
      .offset_low = address,
      .selector = GDT_KERNEL_CODE_SELECTOR,
      .ist = vector == EXCEPTION_DOUBLE_FAULT ? DOUBLE_FAULT_IST : 0,
      /* Ring-zero interrupt gates clear IF on entry. */
      .attributes = IDT_GATE_PRESENT | IDT_GATE_INTERRUPT64,
      .offset_mid = address >> 16,
      .offset_high = address >> 32,
    };
  }

  idt_load();
}

void idt_load(void)
{
  const struct descriptor_table_pointer idtr = {
    .limit = sizeof(idt) - 1,
    .base = (uintptr_t)idt,
  };
  __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

static void report_page_fault(uint64_t error, uint64_t address)
{
  klog("page fault: cr2=0x%lx %s %s %s reserved=%u fetch=%u pkey=%u shadow=%u\n",
       address, error & PAGE_FAULT_PROTECTION ? "protection" : "not-present",
       error & PAGE_FAULT_WRITE ? "write" : "read",
       error & PAGE_FAULT_USER ? "user" : "supervisor",
       (unsigned)((error & PAGE_FAULT_RESERVED_BIT) != 0),
       (unsigned)((error & PAGE_FAULT_INSTRUCTION_FETCH) != 0),
       (unsigned)((error & PAGE_FAULT_PROTECTION_KEY) != 0),
       (unsigned)((error & PAGE_FAULT_SHADOW_STACK) != 0));
}

void interrupt_handler(struct exception_frame *frame)
{
  if (frame->vector == APIC_VIRTIO_FS_VECTOR) {
    virtio_fs_pci_interrupt();
    apic_end_interrupt();
    return;
  }
  if (frame->vector == APIC_KEYBOARD_VECTOR) {
    ps2_keyboard_interrupt();
    apic_end_interrupt();
    return;
  }
  if (frame->vector == APIC_TIMER_VECTOR) {
    atomic_fetch_add_explicit(&cpu_current()->timer_interrupts, 1, memory_order_relaxed);
    apic_end_interrupt();
    task_preempt((frame->cs & SELECTOR_RPL_MASK) == SELECTOR_RPL_USER);
    return;
  }
  if (frame->vector == APIC_SPURIOUS_VECTOR) {
    /* A spurious vector has no in-service bit, so it must not receive EOI. */
    return;
  }
  exception_handler(frame);
}

[[noreturn]] void exception_handler(const struct exception_frame *frame)
{
  uint64_t fault_address = read_cr2();
  bool user_exception = (frame->cs & SELECTOR_RPL_MASK) == SELECTOR_RPL_USER &&
    frame->vector < EXCEPTION_VECTOR_COUNT &&
    frame->vector != EXCEPTION_NMI && frame->vector != EXCEPTION_DOUBLE_FAULT &&
    frame->vector != EXCEPTION_MACHINE_CHECK;
  if (!user_exception) {
    klog_panic_begin();
  }

  klog("\nCaelum exception: vector=%lu error=0x%lx rip=0x%lx\n",
       frame->vector, frame->error, frame->rip);
  klog("cs=0x%lx flags=0x%lx rsp=0x%lx ss=0x%lx rbp=0x%lx\n",
       frame->cs, frame->rflags, frame->rsp, frame->ss, frame->rbp);
  klog("rax=0x%lx rbx=0x%lx rcx=0x%lx rdx=0x%lx rsi=0x%lx rdi=0x%lx\n",
       frame->rax, frame->rbx, frame->rcx, frame->rdx, frame->rsi, frame->rdi);
  klog("r8=0x%lx r9=0x%lx r10=0x%lx r11=0x%lx\n",
       frame->r8, frame->r9, frame->r10, frame->r11);
  klog("r12=0x%lx r13=0x%lx r14=0x%lx r15=0x%lx cr3=0x%lx\n",
       frame->r12, frame->r13, frame->r14, frame->r15, read_cr3());

  if (frame->vector == EXCEPTION_PAGE_FAULT) {
    report_page_fault(frame->error, fault_address);
  }

  if (user_exception) {
    user_fault();
  }
  panic("fatal kernel exception");
}
