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
struct boot_info;

#define DEBUG_READ_BYTES 1024

enum debug_inspect_operation {
  DEBUG_INSPECT_REGISTERS = 1,
  DEBUG_INSPECT_RAM,
};

enum debug_inspect_status {
  DEBUG_INSPECT_OK,
  DEBUG_INSPECT_BAD_REQUEST,
  DEBUG_INSPECT_BAD_ADDRESS,
  DEBUG_INSPECT_FAULT,
};

struct debug_registers {
  struct exception_frame frame;
  uint64_t cr3, gs_base, kernel_gs_base;
};

/* QEMU GDB writes the request fields, then a new nonzero request sequence last.
 * It must leave them untouched until completion matches, and only submit while
 * COMPLETE. Results precede completion publication; reads never return a
 * partial successful result. This boot-only mailbox does not extend expiry. */
struct debug_inspect_mailbox {
  uint64_t generation;
  size_t cpu;
  enum debug_inspect_operation operation;
  uintptr_t address;
  size_t length;
  _Atomic uint64_t request;
  enum debug_inspect_status status;
  size_t bytes;
  struct debug_registers registers;
  uint8_t data[DEBUG_READ_BYTES];
  _Atomic uint64_t completion;
};

extern struct debug_inspect_mailbox arch_debug_inspect;

/* Assembly records the load's exact return stack; keep rsp at offset zero. */
struct debug_probe {
  uintptr_t rsp, address;
  uint64_t generation;
  volatile bool active;
};

extern struct debug_probe arch_debug_probe;

bool arch_debug_load8(uintptr_t address, uint8_t *output,
                      struct debug_probe *probe);
void arch_debug_load8_unsafe(void);
void arch_debug_page_fault_entry(void);
void arch_debug_general_protection_entry(void);
bool arch_debug_fixup(const struct exception_frame *frame);
/* Parked BSP only, COMPLETE generation; exclusive windows, no GS or services. */
void arch_debug_inspect_service(void);
/* Boot BSP, IF=0, before roots/CPUs share the prepared kernel window tables. */
void arch_debug_inspect_prepare(const struct boot_info *boot);
/* Parked BSP only, COMPLETE generation, owned mapped output <= READ_BYTES. */
enum debug_inspect_status arch_debug_read_ram(uint64_t root, uintptr_t address,
                                              uint8_t *output, size_t bytes);

/* Boot BSP, IF=0, before AP startup; all resources remain owned until reboot. */
void arch_debug_enable(const struct boot_info *boot);
void arch_debug_prepare(size_t cpu_count);
void arch_debug_prepare_cpu(struct cpu_local *cpu);
void idt_enable_debug_nmi(void);
/* Once, after CPU/task initialization, before BSP scheduling; no held locks. */
void arch_debug_checkpoint(void);
/* Dedicated IST entry; the handler and every recoverable callee avoid GS. */
void arch_debug_nmi_handler(const struct exception_frame *frame);
void arch_debug_nmi_entry(void);

#endif
