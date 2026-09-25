#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
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
#define XAPIC_MAX_ID 255

static struct cpu_local *bsp_only[1];
static struct cpu_local **cpus;
static size_t cpu_count, cpu_capacity;
/* The handoff must be in the kernel image, mapped by both Limine and Caelum.
 * The AP copies these fields before acknowledging; only then may we reuse it. */
static struct ap_boot handoff;

void arch_smp_prepare(size_t count, uint32_t bsp_lapic_id)
{
  _Static_assert(offsetof(struct ap_boot, root) == AP_BOOT_ROOT_OFFSET, "AP root offset");
  _Static_assert(offsetof(struct ap_boot, stack_top) == AP_BOOT_STACK_OFFSET, "AP stack offset");
  _Static_assert(offsetof(struct ap_boot, cpu) == AP_BOOT_CPU_OFFSET, "AP CPU offset");
  KASSERT(!cpus && count && count <= SIZE_MAX / sizeof(*cpus));
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
  atomic_store_explicit(&cpus[0]->online, true, memory_order_release);
  klog("SMP: BSP APIC %u; %zu CPU(s) reported\n", bsp_lapic_id, count);
}

struct ap_boot *arch_ap_prepare(uint32_t lapic_id)
{
  KASSERT(cpu_count < cpu_capacity && lapic_id <= XAPIC_MAX_ID);
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
  uintptr_t stacks;
  if (vm_alloc(vm_kernel_space(), 2 * AP_STACK_BYTES, PAGE_SIZE, PAGE_WRITE, &stacks) != MM_OK) {
    kfree(cpu);
    panic("cannot allocate AP stacks");
  }
  cpu->lapic_id = lapic_id;
  cpu->index = cpu_count;
  cpu->stack_top = stacks + AP_STACK_BYTES;
  cpu->double_fault_stack_top = stacks + 2 * AP_STACK_BYTES;
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
  struct cpu_local *cpu = handoff.cpu;
  uint32_t previous = apic_timer_remaining();
  unsigned periods = 0;
  /* The BSP keeps IF=0. Its running periodic timer provides a timeout without
   * dispatching interrupts or racing the AP's use of PIT channel 2. */
  while (!atomic_load_explicit(&cpu->online, memory_order_acquire)) {
    uint32_t remaining = apic_timer_remaining();
    if (remaining > previous && ++periods >= AP_STARTUP_TIMER_PERIODS) {
      panic("APIC %u startup timed out", cpu->lapic_id);
    }
    previous = remaining;
    __asm__ volatile("pause");
  }
  klog("SMP: APIC %u online, stack=%p, timer=%u counts per ~8.33 ms\n",
       cpu->lapic_id, (void *)cpu->stack_top, cpu->timer_count);
}

void arch_smp_finish(void)
{
  KASSERT(cpu_count == cpu_capacity);
  klog("SMP: %zu CPU(s) online; APs waiting for scheduler startup\n", cpu_count);
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

[[noreturn]] void arch_ap_main(struct cpu_local *cpu)
{
  gdt_init(&cpu->descriptors, cpu->double_fault_stack_top);
  cpu_install_local(cpu);
  idt_load();
  cpu->active_space = arch_kernel_space();
  KASSERT(read_cr3() == cpu->active_space->root);
  apic_init();
  KASSERT(apic_id() == cpu->lapic_id);
  arch_user_init();
  arch_syscall_init();

  atomic_store_explicit(&cpu->online, true, memory_order_release);
  task_schedule();
}

void arch_cpu_reschedule(size_t index)
{
  struct cpu_local *cpu = arch_cpu_at(index);
  KASSERT(cpu && atomic_load_explicit(&cpu->online, memory_order_acquire));
  apic_send_reschedule(cpu->lapic_id);
}
