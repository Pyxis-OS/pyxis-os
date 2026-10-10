#include <arch/cpu.h>
#include <arch/debug.h>
#include <arch/syscall.h>
#include <kernel/memory.h>
#include <kernel/mm/types.h>

struct debug_probe arch_debug_probe ARCH_DEBUG_DATA;

bool arch_debug_fixup(const struct exception_frame *frame)
{
  _Static_assert(offsetof(struct debug_probe, rsp) == 0,
                 "guarded-load assembly writes the saved RSP at offset zero");
  uintptr_t instruction;
  switch (arch_debug_probe.width) {
  case 8:
    instruction = (uintptr_t)arch_debug_load8_unsafe;
    break;
  case 16:
    instruction = (uintptr_t)arch_debug_load16_unsafe;
    break;
  case 32:
    instruction = (uintptr_t)arch_debug_load32_unsafe;
    break;
  case 64:
    instruction = (uintptr_t)arch_debug_load64_unsafe;
    break;
  default:
    return false;
  }
  if (frame->rip != instruction ||
      !arch_debug_probe.active ||
      atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) !=
        DEBUG_STOP_COMPLETE ||
      arch_debug_probe.generation != arch_debug_stop.generation ||
      cpu_initial_apic_id() != arch_debug_stop.cpus[0].lapic_id) {
    return false;
  }
  const struct debug_cpu_snapshot *bsp = &arch_debug_stop.cpus[0];
  uint16_t ss;
  __asm__ volatile("mov %%ss, %0" : "=r"(ss));
  if (frame->cs != GDT_KERNEL_CODE_SELECTOR || frame->ss != ss ||
      frame->rsp != arch_debug_probe.rsp ||
      frame->rsp < bsp->stack_base + PAGE_SIZE ||
      frame->rsp > bsp->stack_top - sizeof(uint64_t) ||
      frame->rdi != arch_debug_probe.address ||
      (frame->rflags & (RFLAGS_INTERRUPT_ENABLE | RFLAGS_TRAP |
                       RFLAGS_DIRECTION | RFLAGS_NESTED_TASK))) {
    return false;
  }
  if (frame->vector == EXCEPTION_PAGE_FAULT) {
    uint64_t address = read_cr2();
    return address >= arch_debug_probe.address &&
      address - arch_debug_probe.address < arch_debug_probe.width / 8;
  }
  return frame->vector == EXCEPTION_GENERAL_PROTECTION && !frame->error;
}

enum debug_inspect_status arch_debug_read_registers(size_t cpu, struct debug_registers *out)
{
  if (atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) != DEBUG_STOP_COMPLETE ||
      cpu_initial_apic_id() != arch_debug_stop.cpus[0].lapic_id ||
      cpu >= arch_debug_stop.cpu_count ||
      atomic_load_explicit(&arch_debug_stop.cpus[cpu].ack_generation, memory_order_acquire) !=
        arch_debug_stop.generation) {
    return DEBUG_INSPECT_BAD_REQUEST;
  }
  const struct debug_cpu_snapshot *snapshot = &arch_debug_stop.cpus[cpu];
  *out = (struct debug_registers){
    .frame = snapshot->frame, .cr3 = snapshot->cr3,
    .gs_base = snapshot->gs_base, .kernel_gs_base = snapshot->kernel_gs_base,
    .fs_base = snapshot->fs_base, .ds = snapshot->ds, .es = snapshot->es,
    .fs = snapshot->fs, .gs = snapshot->gs,
  };
  return DEBUG_INSPECT_OK;
}
