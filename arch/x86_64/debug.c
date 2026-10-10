#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/debug.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/debug.h>
#include <kernel/fb/early_console.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>

#define DEBUG_ACQUIRE_NS UINT64_C(1000000000)

struct debug_stop_state arch_debug_stop ARCH_DEBUG_DATA;
bool arch_debug_enabled ARCH_DEBUG_DATA;
static struct debug_cpu_snapshot *by_apic[XAPIC_CPU_LIMIT] ARCH_DEBUG_DATA;
static atomic_bool incomplete_claim ARCH_DEBUG_DATA;
static atomic_bool bsp_requested ARCH_DEBUG_DATA;
static bool origin_frame_saved ARCH_DEBUG_DATA;
static struct debug_registers origin_registers ARCH_DEBUG_DATA;

static ARCH_DEBUG_CODE void capture_registers(struct debug_registers *out,
                              const struct exception_frame *frame)
{
  out->frame = *frame;
  out->cr3 = read_cr3();
  out->gs_base = read_msr(IA32_GS_BASE);
  out->kernel_gs_base = read_msr(IA32_KERNEL_GS_BASE);
  out->fs_base = read_msr(IA32_FS_BASE);
  __asm__ volatile("mov %%ds, %0" : "=r"(out->ds));
  __asm__ volatile("mov %%es, %0" : "=r"(out->es));
  __asm__ volatile("mov %%fs, %0" : "=r"(out->fs));
  __asm__ volatile("mov %%gs, %0" : "=r"(out->gs));
}

static ARCH_DEBUG_CODE uint64_t deadline_after(uint64_t interval)
{
  uint64_t now = arch_monotonic_ns();
  return interval > UINT64_MAX - now ? UINT64_MAX : now + interval;
}

void arch_debug_enable(const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !arch_cpu_count());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  arch_debug_enabled = true;
  arch_debug_inspect_prepare(boot);
}

void arch_debug_prepare_cpu(struct cpu_local *cpu)
{
  KASSERT(cpu_current() == cpu_bsp() && arch_debug_enabled);
  KASSERT(cpu->index < arch_debug_stop.cpu_count && cpu->lapic_id <= XAPIC_MAX_ID);
  struct debug_cpu_snapshot *snapshot = &arch_debug_stop.cpus[cpu->index];
  uintptr_t base;
  if (vm_reserve(vm_kernel_space(), DEBUG_STACK_BYTES + 2 * PAGE_SIZE,
        PAGE_SIZE, &base) != MM_OK) {
    panic("cannot reserve debugger stack");
  }
  /* Caller-owned backing in one reservation; both guards remain unmapped.
   * Allocation failure is fatal boot failure, retaining partial ownership. */
  for (size_t offset = PAGE_SIZE; offset < PAGE_SIZE + DEBUG_STACK_BYTES;
       offset += PAGE_SIZE) {
    phys_addr_t physical = pmm_alloc(1);
    if (!physical || vm_map(vm_kernel_space(), base + offset, physical,
          PAGE_WRITE) != MM_OK) {
      panic("cannot back debugger stack");
    }
    memset((void *)(base + offset), 0, PAGE_SIZE);
  }
  snapshot->stack_base = base;
  snapshot->stack_top = base + PAGE_SIZE + DEBUG_STACK_BYTES;
  snapshot->lapic_id = cpu->lapic_id;
  cpu->descriptors.tss.ist[DEBUG_NMI_IST - 1] = snapshot->stack_top;
  by_apic[cpu->lapic_id] = snapshot;
}

void arch_debug_prepare(size_t cpu_count)
{
  KASSERT(arch_debug_enabled && cpu_count && cpu_count <= XAPIC_CPU_LIMIT);
  arch_debug_stop.cpus = kmalloc(cpu_count * sizeof(*arch_debug_stop.cpus));
  if (!arch_debug_stop.cpus) {
    panic("cannot allocate debugger CPU snapshots");
  }
  memset(arch_debug_stop.cpus, 0, cpu_count * sizeof(*arch_debug_stop.cpus));
  arch_debug_stop.cpu_count = cpu_count;
  arch_debug_prepare_cpu(cpu_bsp());
  idt_enable_debug_nmi();
  atomic_store_explicit(&arch_debug_stop.phase, DEBUG_STOP_PREPARED,
                        memory_order_release);
}

static ARCH_DEBUG_CODE bool all_acknowledged(void)
{
  for (size_t i = 0; i < arch_debug_stop.cpu_count; ++i) {
    if (atomic_load_explicit(&arch_debug_stop.cpus[i].ack_generation,
          memory_order_acquire) != arch_debug_stop.generation) {
      return false;
    }
  }
  return true;
}

[[noreturn]] static ARCH_DEBUG_CODE void incomplete_stop(void)
{
  bool expected = false;
  if (atomic_compare_exchange_strong_explicit(&incomplete_claim, &expected, true,
        memory_order_acq_rel, memory_order_acquire)) {
    enum debug_stop_phase phase = atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire);
    for (size_t i = 0; i < arch_debug_stop.cpu_count; ++i) {
      /* A pending claimant has not published its generation or any frames. */
      arch_debug_stop.cpus[i].missing = phase == DEBUG_STOP_PENDING ||
        atomic_load_explicit(&arch_debug_stop.cpus[i].ack_generation,
          memory_order_acquire) != arch_debug_stop.generation;
    }
    atomic_store_explicit(&arch_debug_stop.phase, DEBUG_STOP_INCOMPLETE,
                          memory_order_release);
  }
  /* No ordinary panic/log locks: a missing or stopped CPU may own them. */
  for (;;) {
    arch_clock_maintain();
    __asm__ volatile("pause");
  }
}

static ARCH_DEBUG_CODE void advance_phase(enum debug_stop_phase from,
                                           enum debug_stop_phase to)
{
  if (atomic_load_explicit(&incomplete_claim, memory_order_acquire) ||
      !atomic_compare_exchange_strong_explicit(&arch_debug_stop.phase, &from, to,
          memory_order_acq_rel, memory_order_acquire)) {
    incomplete_stop();
  }
}

static ARCH_DEBUG_CODE void request_bsp(uint64_t generation)
{
  if (atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) != DEBUG_STOP_REQUESTED ||
      arch_debug_stop.generation != generation) {
    return;
  }
  bool expected = false;
  if (!atomic_compare_exchange_strong_explicit(&bsp_requested, &expected, true,
        memory_order_acq_rel, memory_order_acquire)) {
    return;
  }
  if (atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) != DEBUG_STOP_REQUESTED ||
      arch_debug_stop.generation != generation) {
    return;
  }
  while (!apic_try_send_nmi(arch_debug_stop.cpus[0].lapic_id)) {
    arch_clock_maintain();
    if (arch_monotonic_ns() >= arch_debug_stop.acquire_deadline) {
      incomplete_stop();
    }
    __asm__ volatile("pause");
  }
}

static ARCH_DEBUG_CODE void service_stop(uint64_t generation)
{
  if (arch_debug_stop.terminal && !debug_stop_begin(generation, true)) {
    if (debug_stop_retained()) {
      incomplete_stop();
    }
    advance_phase(DEBUG_STOP_REQUESTED, DEBUG_STOP_RELEASED);
    atomic_store_explicit(&arch_debug_stop.resume_generation, generation, memory_order_release);
    return;
  }
  advance_phase(DEBUG_STOP_REQUESTED, DEBUG_STOP_ACQUIRING);
  for (size_t i = 0; i < arch_debug_stop.cpu_count; ++i) {
    struct debug_cpu_snapshot *snapshot = &arch_debug_stop.cpus[i];
    if (atomic_load_explicit(&snapshot->ack_generation, memory_order_acquire) == generation) {
      continue;
    }
    for (;;) {
      arch_clock_maintain();
      if (arch_monotonic_ns() >= arch_debug_stop.acquire_deadline) {
        incomplete_stop();
      }
      if (apic_try_send_nmi(snapshot->lapic_id)) {
        break;
      }
      __asm__ volatile("pause");
    }
  }
  while (!all_acknowledged()) {
    arch_clock_maintain();
    if (arch_monotonic_ns() >= arch_debug_stop.acquire_deadline) {
      incomplete_stop();
    }
    __asm__ volatile("pause");
  }
  advance_phase(DEBUG_STOP_ACQUIRING, DEBUG_STOP_COMPLETE);
  debug_stop_run(generation);
  if (arch_debug_stop.terminal) {
    incomplete_stop();
  }
  /* Transport has restored ordinary ownership before any peer can run. */
  advance_phase(DEBUG_STOP_COMPLETE, DEBUG_STOP_RELEASED);
  atomic_store_explicit(&arch_debug_stop.resume_generation, generation, memory_order_release);
}

uint64_t arch_debug_nmi_handler(const struct exception_frame *frame)
{
  uint32_t id = cpu_initial_apic_id();
  struct debug_cpu_snapshot *snapshot = id <= XAPIC_MAX_ID ? by_apic[id] : NULL;
  enum debug_stop_phase phase = atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire);
  uint64_t generation = arch_debug_stop.generation;
  if (!snapshot || (phase != DEBUG_STOP_REQUESTED && phase != DEBUG_STOP_ACQUIRING &&
      phase != DEBUG_STOP_INCOMPLETE) ||
      (phase == DEBUG_STOP_REQUESTED && snapshot != arch_debug_stop.cpus &&
       snapshot != &arch_debug_stop.cpus[arch_debug_stop.origin_cpu]) ||
      atomic_load_explicit(&snapshot->ack_generation, memory_order_relaxed) == generation) {
    exception_handler(frame);
  }
  struct debug_registers registers;
  capture_registers(&registers, frame);
  if (origin_frame_saved && snapshot == &arch_debug_stop.cpus[arch_debug_stop.origin_cpu]) {
    /* Export the fault, while assembly retains its separate NMI return frame. */
    registers = origin_registers;
  }
  snapshot->frame = registers.frame;
  snapshot->cr3 = registers.cr3;
  snapshot->gs_base = registers.gs_base;
  snapshot->kernel_gs_base = registers.kernel_gs_base;
  snapshot->fs_base = registers.fs_base;
  snapshot->ds = registers.ds;
  snapshot->es = registers.es;
  snapshot->fs = registers.fs;
  snapshot->gs = registers.gs;
  atomic_store_explicit(&snapshot->ack_generation, generation, memory_order_release);
  if (snapshot == arch_debug_stop.cpus) {
    if (phase == DEBUG_STOP_INCOMPLETE || arch_monotonic_ns() >= arch_debug_stop.acquire_deadline) {
      incomplete_stop();
    }
    service_stop(generation);
  } else {
    if (phase == DEBUG_STOP_REQUESTED) {
      request_bsp(generation);
    }
    while (atomic_load_explicit(&arch_debug_stop.resume_generation, memory_order_acquire) != generation) {
      __asm__ volatile("pause");
    }
  }
  return generation;
}

void arch_debug_nmi_exit(uint64_t generation)
{
  struct debug_cpu_snapshot *snapshot = by_apic[cpu_initial_apic_id()];
  /* No stop-state access follows this ACK; IRET still blocks NMI until return. */
  atomic_store_explicit(&snapshot->exit_generation, generation, memory_order_release);
}

static ARCH_DEBUG_CODE void finish_entry(uint64_t generation)
{
  for (size_t i = 0; i < arch_debug_stop.cpu_count; ++i) {
    struct debug_cpu_snapshot *snapshot = &arch_debug_stop.cpus[i];
    if (atomic_load_explicit(&snapshot->ack_generation, memory_order_acquire) != generation) {
      continue;
    }
    while (atomic_load_explicit(&snapshot->exit_generation, memory_order_acquire) != generation) {
      arch_clock_maintain();
      __asm__ volatile("pause");
    }
  }
  advance_phase(DEBUG_STOP_RELEASED, DEBUG_STOP_PREPARED);
}

static ARCH_DEBUG_CODE void await_entry(struct debug_cpu_snapshot *snapshot)
{
  uint64_t exit_generation = atomic_load_explicit(&snapshot->exit_generation, memory_order_acquire);
  uint64_t pending_deadline = deadline_after(DEBUG_ACQUIRE_NS);
  for (;;) {
    arch_clock_maintain();
    uint64_t now = arch_monotonic_ns();
    uint64_t exited = atomic_load_explicit(&snapshot->exit_generation, memory_order_acquire);
    if (exited != exit_generation) {
      /* This caller may have parked through a whole generation. A new pending
       * claimant must receive its own budget, not the expired pre-NMI value. */
      pending_deadline = deadline_after(DEBUG_ACQUIRE_NS);
      exit_generation = exited;
    }
    enum debug_stop_phase phase = atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire);
    if (phase == DEBUG_STOP_PREPARED) {
      return;
    }
    if (phase == DEBUG_STOP_INCOMPLETE) {
      incomplete_stop();
    }
    if (phase == DEBUG_STOP_PENDING) {
      if (now >= pending_deadline &&
          atomic_load_explicit(&snapshot->exit_generation, memory_order_acquire) == exit_generation &&
          atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) == DEBUG_STOP_PENDING) {
        incomplete_stop();
      }
    } else if (phase == DEBUG_STOP_REQUESTED || phase == DEBUG_STOP_ACQUIRING) {
      uint64_t generation = arch_debug_stop.generation;
      uint64_t deadline = arch_debug_stop.acquire_deadline;
      if (atomic_load_explicit(&snapshot->exit_generation, memory_order_acquire) != exit_generation ||
          atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) != phase ||
          arch_debug_stop.generation != generation) {
        continue;
      }
      if (now >= deadline) {
        incomplete_stop();
      }
      /* Only the BSP may help a captured AP origin request service. AP callers
       * await the BSP's acquisition NMI instead of entering REQUESTED themselves. */
      if (phase == DEBUG_STOP_REQUESTED && snapshot == arch_debug_stop.cpus &&
          atomic_load_explicit(&snapshot->ack_generation, memory_order_acquire) != generation &&
          atomic_load_explicit(&arch_debug_stop.cpus[arch_debug_stop.origin_cpu].ack_generation,
            memory_order_acquire) == generation) {
        request_bsp(generation);
      }
    }
    /* COMPLETE and RELEASED have no acquisition expiry. */
    __asm__ volatile("pause");
  }
}

static ARCH_DEBUG_CODE bool enter_stop(const struct exception_frame *frame, uint64_t fault_address,
                        enum debug_stop_reason reason)
{
  uint64_t flags = cpu_save_interrupts();
  uint32_t id = cpu_initial_apic_id();
  struct debug_cpu_snapshot *snapshot = id <= XAPIC_MAX_ID ? by_apic[id] : NULL;
  if (!arch_debug_enabled || !snapshot ||
      (reason == DEBUG_STOP_CHECKPOINT && snapshot != arch_debug_stop.cpus)) {
    cpu_restore_interrupts(flags);
    return false;
  }
  for (;;) {
    enum debug_stop_phase phase = atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire);
    if (phase != DEBUG_STOP_PREPARED) {
      if (phase == DEBUG_STOP_DISABLED || reason == DEBUG_STOP_CHECKPOINT) {
        cpu_restore_interrupts(flags);
        return false;
      }
      /* Keep this caller's original frame/reason until confirmed rearming. */
      await_entry(snapshot);
      continue;
    }
    if (!debug_network_ready()) {
      cpu_restore_interrupts(flags);
      return false;
    }
    if (atomic_compare_exchange_strong_explicit(&arch_debug_stop.phase, &phase, DEBUG_STOP_PENDING,
          memory_order_acq_rel, memory_order_acquire)) {
      break;
    }
  }
  if (arch_debug_stop.generation == UINT64_MAX) {
    advance_phase(DEBUG_STOP_PENDING, DEBUG_STOP_PREPARED);
    cpu_restore_interrupts(flags);
    return false;
  }
  uint64_t generation = ++arch_debug_stop.generation;
  arch_debug_stop.origin_cpu = snapshot - arch_debug_stop.cpus;
  arch_debug_stop.terminal = reason != DEBUG_STOP_CHECKPOINT;
  arch_debug_stop.reason = reason;
  arch_debug_stop.fault_address = fault_address;
  origin_frame_saved = frame != NULL;
  if (frame) {
    capture_registers(&origin_registers, frame);
  }
  if (!arch_debug_stop.terminal && !debug_stop_begin(generation, false)) {
    if (debug_stop_retained()) {
      incomplete_stop();
    }
    advance_phase(DEBUG_STOP_PENDING, DEBUG_STOP_PREPARED);
    cpu_restore_interrupts(flags);
    return false;
  }
  arch_debug_stop.acquire_deadline = deadline_after(DEBUG_ACQUIRE_NS);
  atomic_store_explicit(&bsp_requested, snapshot == arch_debug_stop.cpus, memory_order_relaxed);
  advance_phase(DEBUG_STOP_PENDING, DEBUG_STOP_REQUESTED);
  while (!apic_try_send_nmi(snapshot->lapic_id)) {
    arch_clock_maintain();
    if (arch_monotonic_ns() >= arch_debug_stop.acquire_deadline) {
      incomplete_stop();
    }
    __asm__ volatile("pause");
  }
  for (;;) {
    arch_clock_maintain();
    uint64_t now = arch_monotonic_ns();
    enum debug_stop_phase phase = atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire);
    if (phase == DEBUG_STOP_RELEASED) {
      break;
    }
    if (phase == DEBUG_STOP_INCOMPLETE ||
        ((phase == DEBUG_STOP_REQUESTED || phase == DEBUG_STOP_ACQUIRING) &&
         now >= arch_debug_stop.acquire_deadline &&
         atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) == phase)) {
      incomplete_stop();
    }
    __asm__ volatile("pause");
  }
  finish_entry(generation);
  cpu_restore_interrupts(flags);
  return reason == DEBUG_STOP_CHECKPOINT;
}

bool arch_debug_checkpoint(void)
{
  return enter_stop(NULL, 0, DEBUG_STOP_CHECKPOINT);
}

bool arch_debug_terminal(const struct exception_frame *frame, uint64_t fault_address,
                         enum debug_stop_reason reason)
{
  if (frame && (frame->vector == EXCEPTION_NMI ||
      frame->vector == EXCEPTION_DOUBLE_FAULT || frame->vector == EXCEPTION_MACHINE_CHECK)) {
    return false;
  }
  if (arch_debug_enabled) {
    early_console_panic_message_wait();
  }
  return enter_stop(frame, fault_address, reason);
}
