#include <arch/descriptors.h>
#include <stddef.h>

struct tss64 {
  uint32_t reserved0;
  uint64_t rsp[3];
  uint64_t reserved1;
  uint64_t ist[7];
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iomap_base;
} __attribute__((packed));

static struct tss64 tss;
static uint64_t gdt[5] __attribute__((aligned(16)));
static char double_fault_stack[16384] __attribute__((aligned(4096)));
extern char bootstrap_stack_top[];

void gdt_init(void)
{
  _Static_assert(sizeof(struct tss64) == 104, "TSS hardware layout");
  tss.rsp[0] = (uintptr_t)bootstrap_stack_top;
  tss.ist[0] = (uintptr_t)(double_fault_stack + sizeof(double_fault_stack));
  tss.iomap_base = sizeof(tss);
  gdt[1] = UINT64_C(0x00af9a000000ffff);
  gdt[2] = UINT64_C(0x00cf92000000ffff);
  uintptr_t base = (uintptr_t)&tss;
  uint64_t limit = sizeof(tss) - 1;
  gdt[3] = limit | ((base & 0xffffff) << 16) | (UINT64_C(0x89) << 40) |
           (((base >> 24) & 0xff) << 56);
  gdt[4] = base >> 32;
  const struct {
    uint16_t limit;
    uint64_t base;
  } __attribute__((packed)) gdtr = {sizeof(gdt) - 1, (uintptr_t)gdt};
  __asm__ volatile(
    "lgdt %0\n"
    "pushq $8\n"
    "leaq 1f(%%rip), %%rax\n"
    "pushq %%rax\n"
    "lretq\n"
    "1: mov $16, %%eax\n"
    "mov %%ax, %%ds\n"
    "mov %%ax, %%es\n"
    "mov %%ax, %%ss\n"
    "xor %%eax, %%eax\n"
    "mov %%ax, %%fs\n"
    "mov %%ax, %%gs\n"
    "mov $24, %%eax\n"
    "ltr %%ax\n"
    : : "m"(gdtr) : "rax", "memory");
}
