#include <arch/descriptors.h>
#include <arch/layout.h>
#include <stddef.h>

#define GDT_PRESENT (UINT64_C(1) << 47)
#define GDT_CODE_OR_DATA (UINT64_C(1) << 44)
#define GDT_DPL_USER (UINT64_C(3) << 45)
#define GDT_EXECUTABLE (UINT64_C(1) << 43)
#define GDT_READABLE_OR_WRITABLE (UINT64_C(1) << 41)
#define GDT_LONG_MODE (UINT64_C(1) << 53)
#define GDT_DEFAULT_32BIT (UINT64_C(1) << 54)
#define GDT_PAGE_GRANULARITY (UINT64_C(1) << 55)
#define GDT_TSS_AVAILABLE (UINT64_C(9) << 40)
#define GDT_FLAT_LIMIT (UINT64_C(0xffff) | (UINT64_C(0xf) << 48))
#define DOUBLE_FAULT_STACK_BYTES (16 * 1024)

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
/* A 64-bit TSS descriptor occupies two consecutive GDT entries. */
static uint64_t gdt[GDT_TSS_INDEX + 2] __attribute__((aligned(16)));
static char double_fault_stack[DOUBLE_FAULT_STACK_BYTES]
  __attribute__((aligned(ARCH_PAGE_SIZE)));

static void prepare_tss(void)
{
  _Static_assert(sizeof(struct tss64) == 104, "TSS hardware layout");

  /* IDT IST values are one-based; zero means no IST stack switch. */
  tss.ist[DOUBLE_FAULT_IST - 1] =
    (uintptr_t)(double_fault_stack + sizeof(double_fault_stack));

  /* Put the I/O bitmap beyond the TSS limit: this TSS contains no bitmap. */
  tss.iomap_base = sizeof(tss);
}

static void install_tss_descriptor(void)
{
  uint64_t base = (uintptr_t)&tss;

  /* The low descriptor interleaves base[23:0] and base[31:24] with flags.
   * The next GDT entry holds base[63:32]; its upper half must stay zero. */
  uint64_t base_low24 = (base & UINT64_C(0xffffff)) << 16;
  uint64_t base_high8 = ((base >> 24) & UINT64_C(0xff)) << 56;

  gdt[GDT_TSS_INDEX] = (sizeof(tss) - 1) | base_low24 | base_high8 |
                       GDT_PRESENT | GDT_TSS_AVAILABLE;
  gdt[GDT_TSS_INDEX + 1] = base >> 32;
}

static void load_gdt(void)
{
  const struct descriptor_table_pointer gdtr = {
    .limit = sizeof(gdt) - 1,
    .base = (uintptr_t)gdt,
  };

  /* LGDT alone does not reload cached segment descriptors. A far return loads
   * CS; reload the data selectors too, then LTR enables the TSS's IST stacks. */
  __asm__ volatile(
    "lgdt %[gdtr]\n"
    "pushq %[code]\n"
    "leaq 1f(%%rip), %%rax\n"
    "pushq %%rax\n"
    "lretq\n"
    "1: mov %[data], %%eax\n"
    "mov %%ax, %%ds\n"
    "mov %%ax, %%es\n"
    "mov %%ax, %%ss\n"
    "xor %%eax, %%eax\n"
    "mov %%ax, %%fs\n"
    "mov %%ax, %%gs\n"
    "mov %[tss], %%eax\n"
    "ltr %%ax\n"
    : : [gdtr] "m"(gdtr), [code] "i"(GDT_KERNEL_CODE_SELECTOR),
        [data] "i"(GDT_KERNEL_DATA_SELECTOR), [tss] "i"(GDT_TSS_SELECTOR)
    : "rax", "memory");
}

void gdt_init(void)
{
  prepare_tss();

  /* Ring zero, base zero. The code descriptor must have L=1 and D=0 for
   * 64-bit execution. Retain flat limits and the data descriptor's D/B bit. */
  gdt[GDT_KERNEL_CODE_INDEX] = GDT_FLAT_LIMIT | GDT_PRESENT | GDT_CODE_OR_DATA |
                               GDT_EXECUTABLE | GDT_READABLE_OR_WRITABLE |
                               GDT_LONG_MODE | GDT_PAGE_GRANULARITY;
  gdt[GDT_KERNEL_DATA_INDEX] = GDT_FLAT_LIMIT | GDT_PRESENT | GDT_CODE_OR_DATA |
                               GDT_READABLE_OR_WRITABLE | GDT_DEFAULT_32BIT |
                               GDT_PAGE_GRANULARITY;

  gdt[GDT_USER_CODE_INDEX] = GDT_FLAT_LIMIT | GDT_PRESENT | GDT_CODE_OR_DATA |
                               GDT_EXECUTABLE | GDT_READABLE_OR_WRITABLE |
                               GDT_LONG_MODE | GDT_PAGE_GRANULARITY |
                               GDT_DPL_USER;
  gdt[GDT_USER_DATA_INDEX] = GDT_FLAT_LIMIT | GDT_PRESENT | GDT_CODE_OR_DATA |
                               GDT_READABLE_OR_WRITABLE | GDT_DEFAULT_32BIT |
                               GDT_PAGE_GRANULARITY | GDT_DPL_USER;

  install_tss_descriptor();
  load_gdt();
}

void gdt_set_kernel_stack(uintptr_t stack_top)
{
  tss.rsp[0] = stack_top;
}
