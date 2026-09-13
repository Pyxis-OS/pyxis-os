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

static void prepare_tss(struct cpu_descriptors *tables, uintptr_t stack_top)
{
  _Static_assert(sizeof(struct tss64) == 104, "TSS hardware layout");

  /* IDT IST values are one-based; zero means no IST stack switch. */
  tables->tss.ist[DOUBLE_FAULT_IST - 1] = stack_top;

  /* Put the I/O bitmap beyond the TSS limit: this TSS contains no bitmap. */
  tables->tss.iomap_base = sizeof(tables->tss);
}

static void install_tss_descriptor(struct cpu_descriptors *tables)
{
  uint64_t base = (uintptr_t)&tables->tss;

  /* The low descriptor interleaves base[23:0] and base[31:24] with flags.
   * The next GDT entry holds base[63:32]; its upper half must stay zero. */
  uint64_t base_low24 = (base & UINT64_C(0xffffff)) << 16;
  uint64_t base_high8 = ((base >> 24) & UINT64_C(0xff)) << 56;

  tables->gdt[GDT_TSS_INDEX] = (sizeof(tables->tss) - 1) | base_low24 | base_high8 |
                       GDT_PRESENT | GDT_TSS_AVAILABLE;
  tables->gdt[GDT_TSS_INDEX + 1] = base >> 32;
}

static void load_gdt(struct cpu_descriptors *tables)
{
  const struct descriptor_table_pointer gdtr = {
    .limit = sizeof(tables->gdt) - 1,
    .base = (uintptr_t)tables->gdt,
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

void gdt_init(struct cpu_descriptors *tables, uintptr_t double_fault_stack_top)
{
  prepare_tss(tables, double_fault_stack_top);
  uint64_t *gdt = tables->gdt;

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

  install_tss_descriptor(tables);
  load_gdt(tables);
}

void gdt_set_kernel_stack(struct cpu_descriptors *tables, uintptr_t stack_top)
{
  tables->tss.rsp[0] = stack_top;
}
