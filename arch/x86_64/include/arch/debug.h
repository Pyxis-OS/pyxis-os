#ifndef ARCH_DEBUG_H
#define ARCH_DEBUG_H

#include <arch/descriptors.h>
#include <kernel/mm/types.h>
#include <kernel/pci.h>
#include <stdatomic.h>
#include <stddef.h>

#define ARCH_DEBUG_CODE __attribute__((section(".text.debug")))
#define ARCH_DEBUG_DATA __attribute__((section(".bss.debug")))
#define DEBUG_STACK_BYTES (16 * 1024)

enum debug_stop_phase {
  DEBUG_STOP_DISABLED,
  DEBUG_STOP_PREPARED,
  DEBUG_STOP_REQUESTED,
  DEBUG_STOP_ACQUIRING,
  DEBUG_STOP_COMPLETE,
  DEBUG_STOP_RELEASED,
  DEBUG_STOP_INCOMPLETE,
};

enum debug_stop_reason { DEBUG_STOP_CHECKPOINT, DEBUG_STOP_PANIC, DEBUG_STOP_FAULT };

struct debug_cpu_snapshot {
  struct exception_frame frame;
  uint64_t cr3, gs_base, kernel_gs_base, fs_base;
  uint16_t ds, es, fs, gs;
  uintptr_t stack_base, stack_top;
  uint32_t lapic_id;
  bool missing;
  _Atomic uint64_t ack_generation, exit_generation;
};

struct debug_stop_state {
  struct debug_cpu_snapshot *cpus;
  size_t cpu_count, origin_cpu;
  uint64_t generation, acquire_deadline, fault_address;
  bool terminal;
  enum debug_stop_reason reason;
  _Atomic enum debug_stop_phase phase;
  _Atomic uint64_t resume_generation;
};

extern struct debug_stop_state arch_debug_stop;
extern bool arch_debug_enabled;
struct cpu_local;
struct boot_info;
struct net_debug_device;

#define DEBUG_READ_BYTES 1024

enum debug_inspect_status {
  DEBUG_INSPECT_OK,
  DEBUG_INSPECT_BAD_REQUEST,
  DEBUG_INSPECT_BAD_ADDRESS,
  DEBUG_INSPECT_FAULT,
};

struct debug_registers {
  struct exception_frame frame;
  uint64_t cr3, gs_base, kernel_gs_base, fs_base;
  uint16_t ds, es, fs, gs;
};

/* Assembly records the load's exact return stack; keep rsp at offset zero. */
struct debug_probe {
  uintptr_t rsp, address;
  uint64_t generation;
  volatile bool active;
  unsigned width;
};
extern struct debug_probe arch_debug_probe;
ARCH_DEBUG_CODE bool arch_debug_load8(uintptr_t address, uint8_t *output, struct debug_probe *probe);
ARCH_DEBUG_CODE bool arch_debug_load16(uintptr_t address, uint16_t *output, struct debug_probe *probe);
ARCH_DEBUG_CODE bool arch_debug_load32(uintptr_t address, uint32_t *output, struct debug_probe *probe);
ARCH_DEBUG_CODE bool arch_debug_load64(uintptr_t address, uint64_t *output, struct debug_probe *probe);
ARCH_DEBUG_CODE void arch_debug_load8_unsafe(void);
ARCH_DEBUG_CODE void arch_debug_load16_unsafe(void);
ARCH_DEBUG_CODE void arch_debug_load32_unsafe(void);
ARCH_DEBUG_CODE void arch_debug_load64_unsafe(void);
ARCH_DEBUG_CODE void arch_debug_page_fault_entry(void);
ARCH_DEBUG_CODE void arch_debug_general_protection_entry(void);
ARCH_DEBUG_CODE bool arch_debug_fixup(const struct exception_frame *frame);

/* Boot BSP, IF=0, before roots/CPUs share the prepared kernel window tables. */
ARCH_DEBUG_CODE void arch_debug_inspect_prepare(const struct boot_info *boot);
/* BSP after CPU preparation, before scheduling; finalize permanent exclusions. */
ARCH_DEBUG_CODE void arch_debug_inspect_finish(void);
ARCH_DEBUG_CODE void arch_debug_inspect_transport(const struct net_debug_device *device);
/* COMPLETE parked BSP only. Results are little endian; no partial success. */
ARCH_DEBUG_CODE enum debug_inspect_status arch_debug_read_registers(size_t cpu, struct debug_registers *out);
ARCH_DEBUG_CODE enum debug_inspect_status arch_debug_read_ram(uint64_t root, uintptr_t address,
                                              uint8_t *output, size_t bytes);
ARCH_DEBUG_CODE enum debug_inspect_status arch_debug_read_phys(phys_addr_t address, unsigned width,
    size_t count, uint8_t *output, size_t capacity);
ARCH_DEBUG_CODE enum debug_inspect_status arch_debug_read_mmio(phys_addr_t address, unsigned width,
    size_t count, uint8_t *output, size_t capacity);
ARCH_DEBUG_CODE enum debug_inspect_status arch_debug_read_pci(unsigned segment,
    struct pci_address address, unsigned offset, unsigned width,
    uint8_t *output, size_t capacity);

ARCH_DEBUG_CODE void arch_debug_enable(const struct boot_info *boot);
ARCH_DEBUG_CODE void arch_debug_prepare(size_t cpu_count);
ARCH_DEBUG_CODE void arch_debug_prepare_cpu(struct cpu_local *cpu);
void idt_enable_debug_nmi(void);
/* Coherent BSP checkpoint, IF saved/restored; false leaves ordinary execution. */
ARCH_DEBUG_CODE bool arch_debug_checkpoint(void);
/* Terminal origin only; false permits existing fatal fallback. */
ARCH_DEBUG_CODE bool arch_debug_terminal(const struct exception_frame *frame, uint64_t fault_address,
                         enum debug_stop_reason reason);
/* GS-independent IST entry. Exit ACK precedes restore/IRET, not proof of IRET. */
ARCH_DEBUG_CODE uint64_t arch_debug_nmi_handler(const struct exception_frame *frame);
ARCH_DEBUG_CODE void arch_debug_nmi_exit(uint64_t generation);
ARCH_DEBUG_CODE void arch_debug_nmi_entry(void);

#endif
