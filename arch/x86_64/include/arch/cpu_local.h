#ifndef ARCH_CPU_LOCAL_H
#define ARCH_CPU_LOCAL_H

#define CPU_SELF_OFFSET 0
#define CPU_SYSCALL_STACK_OFFSET 8
#define CPU_USER_RSP_OFFSET 16

#ifndef __ASSEMBLER__
#include <arch/descriptors.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

struct arch_address_space;

struct cpu_local {
  struct cpu_local *self;
  uintptr_t syscall_stack_top;
  uintptr_t saved_user_rsp;
  struct arch_address_space *active_space;
  size_t index;
  uint32_t lapic_id;
  uint32_t timer_count;
  _Atomic uint64_t timer_interrupts;
  _Atomic uint64_t tlb_flush_ack;
  _Atomic bool online;
  uintptr_t stack_top, double_fault_stack_top;
  struct cpu_descriptors descriptors;
  uint32_t core_id;
  unsigned smt_shift;
  bool topology_known;
};

/* Kernel GS names this CPU. User entry/return exchanges it with the user base.
 * Fatal NMI reporting must not depend on GS during the entry/exit window. */
static inline struct cpu_local *cpu_current(void)
{
  struct cpu_local *cpu;
  __asm__ volatile("movq %%gs:%c1, %0" : "=r"(cpu) : "i"(CPU_SELF_OFFSET));
  return cpu;
}

struct cpu_local *cpu_bsp(void);
void cpu_install_local(struct cpu_local *cpu);
#endif
#endif
