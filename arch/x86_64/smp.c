#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/debug.h>
#include <arch/fp.h>
#include <arch/hwp.h>
#include <arch/paging.h>
#include <arch/smp.h>
#include <arch/syscall.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/task.h>

#define AP_STACK_BYTES (16 * 1024)
#define AP_STARTUP_TIMER_PERIODS 600
#define TLB_FLUSH_POLL_NS UINT64_C(1000000)
#define CPUID_TOPOLOGY_COUNT_MASK 0xffffu
#define CPUID_TOPOLOGY_SHIFT_MASK 0x1fu
#define CPUID_TOPOLOGY_TYPE_SHIFT 8
#define CPUID_TOPOLOGY_TYPE_MASK 0xffu
#define CPUID_TOPOLOGY_TYPE_SMT 1
#define CPUID_EXTENDED_MAX 0x80000000u
#define CPUID_AMD_TOPOLOGY 0x8000001eu
#define CPUID_AMD_TOPOLOGY_EXTENSIONS (1u << 22)
#define CPUID_AMD_THREADS_SHIFT 8
#define CPUID_AMD_THREADS_MASK 0xffu
#define CPUID_FAMILY_SHIFT 8
#define CPUID_FAMILY_MASK 0xfu
#define CPUID_EXTENDED_FAMILY_SHIFT 20
#define CPUID_EXTENDED_FAMILY_MASK 0xffu
#define CPUID_AMD_ZEN_FAMILY 0x17u
#define CPUID_VENDOR_AMD_EBX 0x68747541u
#define CPUID_VENDOR_AMD_EDX 0x69746e65u
#define CPUID_VENDOR_AMD_ECX 0x444d4163u

static struct cpu_local *bsp_only[1];
static struct cpu_local **cpus;
static size_t cpu_count, cpu_capacity;
/* The handoff must be in the kernel image, mapped by both Limine and Caelum.
 * The AP copies these fields before acknowledging; only then may we reuse it. */
static struct ap_boot handoff;
/* Issue after closure publication. Acquire/release RMWs carry an earlier
 * issuer's closure to every later issuer, including a coalesced higher ACK. */
static _Atomic uint64_t tlb_flush_issued;

static void detect_topology(struct cpu_local *cpu)
{
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_VENDOR, &eax, &ebx, &ecx, &edx);
  bool amd = ebx == CPUID_VENDOR_AMD_EBX && edx == CPUID_VENDOR_AMD_EDX &&
    ecx == CPUID_VENDOR_AMD_ECX;
  if (eax >= CPUID_X2APIC_TOPOLOGY) {
    cpuid(CPUID_X2APIC_TOPOLOGY, &eax, &ebx, &ecx, &edx);
    unsigned type = (ecx >> CPUID_TOPOLOGY_TYPE_SHIFT) & CPUID_TOPOLOGY_TYPE_MASK;
    if ((ebx & CPUID_TOPOLOGY_COUNT_MASK) && type == CPUID_TOPOLOGY_TYPE_SMT &&
        edx == cpu->lapic_id) {
      cpu->smt_shift = eax & CPUID_TOPOLOGY_SHIFT_MASK;
      cpu->core_id = edx >> cpu->smt_shift;
      cpu->topology_known = true;
      return;
    }
  }
  if (!amd) {
    return;
  }
  cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
  unsigned family = (eax >> CPUID_FAMILY_SHIFT) & CPUID_FAMILY_MASK;
  if (family == CPUID_FAMILY_MASK) {
    family += (eax >> CPUID_EXTENDED_FAMILY_SHIFT) & CPUID_EXTENDED_FAMILY_MASK;
  }
  /* Before Zen, leaf 1e describes cores per compute unit, not SMT threads. */
  if (family < CPUID_AMD_ZEN_FAMILY) {
    return;
  }
  cpuid(CPUID_EXTENDED_MAX, &eax, &ebx, &ecx, &edx);
  if (eax < CPUID_AMD_TOPOLOGY) {
    return;
  }
  cpuid(CPUID_EXTENDED_FEATURES, &eax, &ebx, &ecx, &edx);
  if (!(ecx & CPUID_AMD_TOPOLOGY_EXTENSIONS)) {
    return;
  }
  /* The extended APIC ID is defined only with the local APIC enabled. */
  cpuid(CPUID_AMD_TOPOLOGY, &eax, &ebx, &ecx, &edx);
  unsigned threads = ((ebx >> CPUID_AMD_THREADS_SHIFT) & CPUID_AMD_THREADS_MASK) + 1;
  if (eax != cpu->lapic_id || (threads & (threads - 1))) {
    return;
  }
  while (threads > 1) {
    ++cpu->smt_shift;
    threads >>= 1;
  }
  cpu->core_id = eax >> cpu->smt_shift;
  cpu->topology_known = true;
}

static void log_topology(const struct cpu_local *cpu)
{
  if (cpu->topology_known) {
    kdebug("SMP: CPU %zu APIC %u core %u, SMT shift %u\n",
         cpu->index, cpu->lapic_id, cpu->core_id, cpu->smt_shift);
  } else {
    kdebug("SMP: CPU %zu APIC %u core unknown (isolated)\n", cpu->index, cpu->lapic_id);
  }
}

void arch_smp_prepare(size_t count, uint32_t bsp_lapic_id)
{
  _Static_assert(offsetof(struct ap_boot, root) == AP_BOOT_ROOT_OFFSET, "AP root offset");
  _Static_assert(offsetof(struct ap_boot, stack_top) == AP_BOOT_STACK_OFFSET, "AP stack offset");
  _Static_assert(offsetof(struct ap_boot, cpu) == AP_BOOT_CPU_OFFSET, "AP CPU offset");
  KASSERT(!cpus && count && count <= SIZE_MAX / sizeof(*cpus));
  if (count > ARCH_CPU_LIMIT) {
    panic("CPU count %zu exceeds the %u-CPU scratch window", count, ARCH_CPU_LIMIT);
  }
  KASSERT(cpu_current() == cpu_bsp() && apic_id() == bsp_lapic_id);
  cpus = count == 1 ? bsp_only : kmalloc(count * sizeof(*cpus));
  if (!cpus) {
    panic("cannot allocate CPU records");
  }
  memset(cpus, 0, count * sizeof(*cpus));
  cpu_capacity = count;
  cpu_count = 1;
  cpus[0] = cpu_bsp();
  cpus[0]->lapic_id = bsp_lapic_id;
  detect_topology(cpus[0]);
  atomic_store_explicit(&cpus[0]->online, true, memory_order_release);
  if (arch_debug_enabled) {
    arch_debug_prepare(count);
  }
  kdebug("SMP: BSP APIC %u; %zu CPU(s) reported\n", bsp_lapic_id, count);
  log_topology(cpus[0]);
}

struct ap_boot *arch_ap_prepare(uint32_t lapic_id)
{
  arch_clock_maintain();
  KASSERT(cpu_count < cpu_capacity && lapic_id != UINT32_MAX);
  for (size_t i = 0; i < cpu_count; ++i) {
    if (cpus[i]->lapic_id == lapic_id) {
      panic("duplicate APIC ID %u", lapic_id);
    }
  }

  struct cpu_local *cpu = kmalloc(sizeof(*cpu));
  if (!cpu) {
    panic("cannot allocate AP state");
  }
  memset(cpu, 0, sizeof(*cpu));
  atomic_init(&cpu->online, false);
  atomic_init(&cpu->timer_interrupts, 0);
  atomic_init(&cpu->tlb_flush_requested, 0);
  atomic_init(&cpu->tlb_flush_ack, 0);
  uintptr_t stacks;
  if (vm_alloc(vm_kernel_space(), 2 * AP_STACK_BYTES, PAGE_SIZE, PAGE_WRITE, &stacks) != MM_OK) {
    kfree(cpu);
    panic("cannot allocate AP stacks");
  }
  cpu->lapic_id = lapic_id;
  cpu->index = cpu_count;
  cpu->stack_top = stacks + AP_STACK_BYTES;
  cpu->double_fault_stack_top = stacks + 2 * AP_STACK_BYTES;
  if (arch_debug_enabled) {
    arch_debug_prepare_cpu(cpu);
  }
  cpus[cpu_count++] = cpu;

  handoff = (struct ap_boot){
    .root = arch_kernel_space()->root,
    .stack_top = cpu->stack_top,
    .cpu = cpu,
  };
  return &handoff;
}

void arch_ap_wait(void)
{
  arch_clock_maintain();
  struct cpu_local *cpu = handoff.cpu;
  uint32_t previous = apic_timer_remaining();
  unsigned periods = 0;
  /* The BSP keeps IF=0. Its running periodic timer provides a timeout without
   * dispatching interrupts or racing the AP's use of PIT channel 2. */
  while (!atomic_load_explicit(&cpu->online, memory_order_acquire)) {
    uint32_t remaining = apic_timer_remaining();
    if (remaining > previous) {
      arch_clock_maintain();
      if (++periods >= AP_STARTUP_TIMER_PERIODS) {
        panic("APIC %u startup timed out", cpu->lapic_id);
      }
    }
    previous = remaining;
    __asm__ volatile("pause");
  }
  arch_clock_bsp_check(cpu->index);
  arch_clock_maintain();
  kdebug("SMP: APIC %u online, stack=%p, timer=%u counts per ~8.33 ms\n",
       cpu->lapic_id, (void *)cpu->stack_top, cpu->timer_count);
  log_topology(cpu);
}

void arch_smp_finish(void)
{
  KASSERT(cpu_count == cpu_capacity);
  kinfo("SMP: %zu CPU(s) online; APs waiting for scheduler startup\n", cpu_count);
  arch_hwp_report();
  arch_clock_select();
  arch_fp_finalize();
}

size_t arch_cpu_count(void)
{
  return cpu_count;
}

size_t arch_cpu_index(void)
{
  return cpu_current()->index;
}

struct cpu_local *arch_cpu_at(size_t index)
{
  KASSERT(index < cpu_count);
  return cpus[index];
}

bool arch_cpus_share_core(size_t first, size_t second)
{
  struct cpu_local *a = arch_cpu_at(first), *b = arch_cpu_at(second);
  return first == second || (a->topology_known && b->topology_known &&
    a->smt_shift == b->smt_shift && a->core_id == b->core_id);
}

[[noreturn]] void arch_ap_main(struct cpu_local *cpu)
{
  gdt_init(&cpu->descriptors, cpu->double_fault_stack_top);
  cpu_install_local(cpu);
  idt_load();
  cpu->active_space = arch_kernel_space();
  KASSERT(read_cr3() == cpu->active_space->root);
  apic_init();
  KASSERT(apic_id() == cpu->lapic_id);
  detect_topology(cpu);
  arch_user_init();
  arch_syscall_init();
  arch_hwp_init();
  arch_clock_ap_prepare();

  atomic_store_explicit(&cpu->online, true, memory_order_release);
  arch_clock_ap_check();
  arch_fp_ap_wait();
  task_schedule();
}

void arch_cpu_reschedule(size_t index)
{
  struct cpu_local *cpu = arch_cpu_at(index);
  KASSERT(cpu && atomic_load_explicit(&cpu->online, memory_order_acquire));
  apic_send_reschedule(cpu->lapic_id);
}

void arch_tlb_flush_interrupt(void)
{
  struct cpu_local *cpu = cpu_current();
  uint64_t generation = atomic_load_explicit(&cpu->tlb_flush_requested,
                                             memory_order_acquire);
  /* PCID and global pages are disabled. The current CR3 flush covers shared
   * kernel translations without changing the root/owner. No request or root
   * pointer is retained by this permanently mapped mailbox. */
  write_cr3(read_cr3());
  atomic_store_explicit(&cpu->tlb_flush_ack, generation, memory_order_release);
}

static uint64_t issue_flush_epoch(void)
{
  uint64_t value = atomic_load_explicit(&tlb_flush_issued, memory_order_acquire);
  for (;;) {
    if (value == UINT64_MAX) {
      return 0;
    }
    if (atomic_compare_exchange_weak_explicit(&tlb_flush_issued, &value,
        value + 1, memory_order_acq_rel, memory_order_acquire)) {
      return value + 1;
    }
  }
}

static void request_flush(struct cpu_local *cpu, uint64_t generation)
{
  uint64_t value = atomic_load_explicit(&cpu->tlb_flush_requested, memory_order_acquire);
  while (value < generation &&
      !atomic_compare_exchange_weak_explicit(&cpu->tlb_flush_requested, &value,
          generation, memory_order_acq_rel, memory_order_acquire)) {
  }
  /* If a larger request/ACK already exists, its issue RMW acquired this
   * issuer's earlier closure. A delayed publisher can therefore use that ACK;
   * a merely increasing number without the issue chain would be unsound. */
}

bool arch_kernel_flush_remote(uint64_t deadline)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  KASSERT(cpus && cpu_count == cpu_capacity);
  KASSERT(cpu_current()->active_space == arch_kernel_space());
  if (arch_monotonic_ns() >= deadline) {
    cpu_restore_interrupts(flags);
    return false;
  }
  uint64_t generation = issue_flush_epoch();
  if (!generation) {
    cpu_restore_interrupts(flags);
    return false;
  }
  /* Membership is fixed after startup. Publish all mailboxes before ringing
   * doorbells; each is permanent and may safely outlive a timed-out initiator. */
  for (size_t i = 0; i < cpu_count; ++i) {
    struct cpu_local *cpu = cpus[i];
    if (atomic_load_explicit(&cpu->online, memory_order_acquire)) {
      request_flush(cpu, generation);
    }
  }
  arch_tlb_flush_interrupt();
  cpu_restore_interrupts(flags);

  for (;;) {
    bool complete = true;
    uint64_t now = arch_monotonic_ns();
    for (size_t i = 0; i < cpu_count; ++i) {
      struct cpu_local *cpu = cpus[i];
      if (atomic_load_explicit(&cpu->online, memory_order_acquire) &&
          atomic_load_explicit(&cpu->tlb_flush_ack, memory_order_acquire) < generation) {
        complete = false;
        if (now < deadline) {
          flags = cpu_save_interrupts();
          (void)apic_try_send_tlb_flush(cpu->lapic_id);
          cpu_restore_interrupts(flags);
        }
      }
    }
    if (complete || now >= deadline) {
      return complete;
    }
    /* One bounded handler per delivery. A request racing an older delivery
     * cannot be stranded by coalescing: retry outstanding targets each poll.
     * No spinlock, scratch mapping or borrower crosses this timed sleep. */
    uint64_t wake = deadline - now > TLB_FLUSH_POLL_NS ?
        now + TLB_FLUSH_POLL_NS : deadline;
    flags = cpu_save_interrupts();
    struct task_wait *wait = task_wait_prepare();
    task_wait_sleep_until(wait, wake);
    cpu_restore_interrupts(flags);
  }
}
