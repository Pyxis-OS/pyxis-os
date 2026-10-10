#include <arch/cpu.h>
#include <arch/debug.h>
#include <arch/syscall.h>
#include <kernel/memory.h>
#include <kernel/mm/types.h>

struct debug_inspect_mailbox arch_debug_inspect;
struct debug_probe arch_debug_probe;

bool arch_debug_fixup(const struct exception_frame *frame)
{
  _Static_assert(offsetof(struct debug_probe, rsp) == 0,
                 "guarded-load assembly writes the saved RSP at offset zero");
  if (frame->rip != (uintptr_t)arch_debug_load8_unsafe ||
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
    return read_cr2() == arch_debug_probe.address;
  }
  return frame->vector == EXCEPTION_GENERAL_PROTECTION && !frame->error;
}

void arch_debug_inspect_service(void)
{
  uint64_t request = atomic_load_explicit(&arch_debug_inspect.request,
                                         memory_order_acquire);
  if (!request || request == atomic_load_explicit(&arch_debug_inspect.completion,
                                                 memory_order_relaxed)) {
    return;
  }
  arch_debug_inspect.bytes = 0;
  memset(&arch_debug_inspect.registers, 0, sizeof(arch_debug_inspect.registers));
  memset(arch_debug_inspect.data, 0, sizeof(arch_debug_inspect.data));
  arch_debug_inspect.status = DEBUG_INSPECT_BAD_REQUEST;
  size_t cpu = arch_debug_inspect.cpu;
  if (atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) ==
        DEBUG_STOP_COMPLETE &&
      arch_debug_inspect.generation == arch_debug_stop.generation &&
      cpu < arch_debug_stop.cpu_count &&
      atomic_load_explicit(&arch_debug_stop.cpus[cpu].ack_generation,
                           memory_order_acquire) == arch_debug_stop.generation) {
    const struct debug_cpu_snapshot *snapshot = &arch_debug_stop.cpus[cpu];
    if (arch_debug_inspect.operation == DEBUG_INSPECT_REGISTERS) {
      arch_debug_inspect.registers = (struct debug_registers){
        .frame = snapshot->frame,
        .cr3 = snapshot->cr3,
        .gs_base = snapshot->gs_base,
        .kernel_gs_base = snapshot->kernel_gs_base,
      };
      arch_debug_inspect.status = DEBUG_INSPECT_OK;
    } else if (arch_debug_inspect.operation == DEBUG_INSPECT_RAM &&
               arch_debug_inspect.length &&
               arch_debug_inspect.length <= DEBUG_READ_BYTES) {
      arch_debug_inspect.status = arch_debug_read_ram(snapshot->cr3,
          arch_debug_inspect.address, arch_debug_inspect.data,
          arch_debug_inspect.length);
      if (arch_debug_inspect.status == DEBUG_INSPECT_OK) {
        arch_debug_inspect.bytes = arch_debug_inspect.length;
      } else {
        memset(arch_debug_inspect.data, 0, sizeof(arch_debug_inspect.data));
      }
    }
  }
  atomic_store_explicit(&arch_debug_inspect.completion, request,
                        memory_order_release);
}
