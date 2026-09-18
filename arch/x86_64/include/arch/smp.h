#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#define AP_BOOT_ROOT_OFFSET 0
#define AP_BOOT_STACK_OFFSET 8
#define AP_BOOT_CPU_OFFSET 16

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

struct cpu_local;
struct ap_boot {
  uint64_t root;
  uintptr_t stack_top;
  struct cpu_local *cpu;
};

/* BSP only, after VM/heap initialization. APs start serially: prepare, publish
 * the returned handoff through the boot adapter, then wait before preparing
 * another. CPU records and stacks remain owned by the kernel until shutdown. */
void arch_smp_prepare(size_t count, uint32_t bsp_lapic_id);
struct ap_boot *arch_ap_prepare(uint32_t lapic_id);
void arch_ap_wait(void);
void arch_smp_finish(void);

/* Stable dense indices after boot_start_cpus(): BSP is always index zero. */
size_t arch_cpu_count(void);
size_t arch_cpu_index(void);

struct cpu_local *arch_cpu_at(size_t index);

[[noreturn]] void arch_ap_entry(struct ap_boot *boot);
[[noreturn]] void arch_ap_main(struct cpu_local *cpu);
#endif
#endif
