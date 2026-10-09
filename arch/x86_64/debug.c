#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/debug.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>

#define DEBUG_STACK_BYTES (16 * 1024)
#define DEBUG_ACQUIRE_NS UINT64_C(1000000000)
#define DEBUG_CHECKPOINT_NS UINT64_C(30000000000)

struct debug_stop_state arch_debug_stop;
bool arch_debug_enabled;
static struct debug_cpu_snapshot *by_apic[XAPIC_CPU_LIMIT];

static uint64_t deadline_after(uint64_t interval)
{
  uint64_t now = arch_monotonic_ns();
  return interval > UINT64_MAX - now ? UINT64_MAX : now + interval;
}

void arch_debug_enable(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !arch_cpu_count());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  arch_debug_enabled = true;
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

static bool all_acknowledged(void)
{
  for (size_t i = 0; i < arch_debug_stop.cpu_count; ++i) {
    if (atomic_load_explicit(&arch_debug_stop.cpus[i].ack_generation,
          memory_order_acquire) != arch_debug_stop.generation) {
      return false;
    }
  }
  return true;
}

[[noreturn]] static void incomplete_stop(void)
{
  for (size_t i = 0; i < arch_debug_stop.cpu_count; ++i) {
    arch_debug_stop.cpus[i].missing =
      atomic_load_explicit(&arch_debug_stop.cpus[i].ack_generation,
        memory_order_acquire) != arch_debug_stop.generation;
  }
  atomic_store_explicit(&arch_debug_stop.phase, DEBUG_STOP_INCOMPLETE,
                        memory_order_release);
  /* No ordinary panic/log locks: a missing or stopped CPU may own them. */
  for (;;) {
    arch_clock_maintain();
    __asm__ volatile("pause");
  }
}

static void service_stop(void)
{
  for (size_t i = 1; i < arch_debug_stop.cpu_count; ++i) {
    struct debug_cpu_snapshot *snapshot = &arch_debug_stop.cpus[i];
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

  arch_debug_stop.expiry = deadline_after(DEBUG_CHECKPOINT_NS);
  atomic_store_explicit(&arch_debug_stop.release_generation, 0,
                        memory_order_relaxed);
  atomic_store_explicit(&arch_debug_stop.phase, DEBUG_STOP_COMPLETE,
                        memory_order_release);
  for (;;) {
    arch_clock_maintain();
    if (atomic_load_explicit(&arch_debug_stop.release_generation,
          memory_order_acquire) == arch_debug_stop.generation ||
        arch_monotonic_ns() >= arch_debug_stop.expiry) {
      break;
    }
    __asm__ volatile("pause");
  }
  atomic_store_explicit(&arch_debug_stop.phase, DEBUG_STOP_RELEASED,
                        memory_order_release);
  atomic_store_explicit(&arch_debug_stop.resume_generation,
                        arch_debug_stop.generation, memory_order_release);
}

void arch_debug_nmi_handler(const struct exception_frame *frame)
{
  uint32_t id = cpu_initial_apic_id();
  struct debug_cpu_snapshot *snapshot = id <= XAPIC_MAX_ID ? by_apic[id] : NULL;
  enum debug_stop_phase phase = atomic_load_explicit(&arch_debug_stop.phase,
                                                    memory_order_acquire);
  if (!snapshot || (phase != DEBUG_STOP_ACQUIRING &&
      phase != DEBUG_STOP_INCOMPLETE) ||
      atomic_load_explicit(&snapshot->ack_generation, memory_order_relaxed)) {
    exception_handler(frame);
  }
  snapshot->frame = *frame;
  snapshot->cr3 = read_cr3();
  snapshot->gs_base = read_msr(IA32_GS_BASE);
  snapshot->kernel_gs_base = read_msr(IA32_KERNEL_GS_BASE);
  atomic_store_explicit(&snapshot->ack_generation, arch_debug_stop.generation,
                        memory_order_release);
  if (snapshot == arch_debug_stop.cpus) {
    if (phase == DEBUG_STOP_INCOMPLETE ||
        arch_monotonic_ns() >= arch_debug_stop.acquire_deadline) {
      incomplete_stop();
    }
    service_stop();
  } else {
    while (atomic_load_explicit(&arch_debug_stop.resume_generation,
          memory_order_acquire) != arch_debug_stop.generation) {
      __asm__ volatile("pause");
    }
  }
}

void arch_debug_checkpoint(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_debug_enabled && cpu_current() == cpu_bsp());
  KASSERT(arch_cpu_count() == arch_debug_stop.cpu_count);
  KASSERT(atomic_load_explicit(&arch_debug_stop.phase, memory_order_relaxed) ==
          DEBUG_STOP_PREPARED);
  arch_debug_stop.generation = 1;
  arch_debug_stop.acquire_deadline = deadline_after(DEBUG_ACQUIRE_NS);
  atomic_store_explicit(&arch_debug_stop.phase, DEBUG_STOP_ACQUIRING,
                        memory_order_release);
  /* Self-NMI supplies a genuine BSP frame and switches service to its IST. */
  while (!apic_try_send_nmi(arch_debug_stop.cpus[0].lapic_id)) {
    arch_clock_maintain();
    if (arch_monotonic_ns() >= arch_debug_stop.acquire_deadline) {
      incomplete_stop();
    }
    __asm__ volatile("pause");
  }
  while (atomic_load_explicit(&arch_debug_stop.phase, memory_order_acquire) !=
         DEBUG_STOP_RELEASED) {
    arch_clock_maintain();
    if (arch_monotonic_ns() >= arch_debug_stop.acquire_deadline) {
      incomplete_stop();
    }
    __asm__ volatile("pause");
  }
  cpu_restore_interrupts(flags);
}
