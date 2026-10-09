#ifndef ARCH_DEBUG_H
#define ARCH_DEBUG_H

#include <arch/descriptors.h>
#include <stdatomic.h>
#include <stddef.h>

enum debug_stop_phase {
  DEBUG_STOP_DISABLED,
  DEBUG_STOP_PREPARED,
  DEBUG_STOP_ACQUIRING,
  DEBUG_STOP_COMPLETE,
  DEBUG_STOP_RELEASED,
  DEBUG_STOP_INCOMPLETE,
};

struct debug_cpu_snapshot {
  struct exception_frame frame;
  uint64_t cr3, gs_base, kernel_gs_base;
  uintptr_t stack_base, stack_top;
  uint32_t lapic_id;
  bool missing;
  _Atomic uint64_t ack_generation;
};

struct debug_stop_state {
  struct debug_cpu_snapshot *cpus;
  size_t cpu_count;
  uint64_t generation, acquire_deadline, expiry;
  _Atomic enum debug_stop_phase phase;
  /* QEMU's GDB may request release of the current complete generation. */
  _Atomic uint64_t release_generation;
  _Atomic uint64_t resume_generation;
};

extern struct debug_stop_state arch_debug_stop;
extern bool arch_debug_enabled;
struct cpu_local;

/* Boot BSP, IF=0, before AP startup; all resources remain owned until reboot. */
void arch_debug_enable(void);
void arch_debug_prepare(size_t cpu_count);
void arch_debug_prepare_cpu(struct cpu_local *cpu);
void idt_enable_debug_nmi(void);
/* Once, after CPU/task initialization, before BSP scheduling; no held locks. */
void arch_debug_checkpoint(void);
/* Dedicated IST entry; the handler and every recoverable callee avoid GS. */
void arch_debug_nmi_handler(const struct exception_frame *frame);
void arch_debug_nmi_entry(void);

#endif
