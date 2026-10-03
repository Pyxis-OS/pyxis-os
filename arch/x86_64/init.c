#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/console.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/descriptors.h>
#include <arch/init.h>
#include <arch/io_apic.h>
#include <arch/keyboard.h>
#include <arch/paging.h>
#include <arch/syscall.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/panic.h>

#define STAR_KERNEL_CS_SHIFT 32
#define STAR_SYSRET_BASE_SHIFT 48

void arch_syscall_init(void)
{
  /* paging_init already checked that this extended CPUID leaf exists. */
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_EXTENDED_FEATURES, &eax, &ebx, &ecx, &edx);
  if (!(edx & CPUID_FEATURE_SYSCALL)) {
    panic("CPU lacks SYSCALL/SYSRET support");
  }

  _Static_assert(GDT_KERNEL_DATA_SELECTOR ==
                 GDT_KERNEL_CODE_SELECTOR + GDT_ENTRY_BYTES,
                 "SYSCALL requires kernel SS immediately after CS");
  _Static_assert(GDT_USER_CODE_SELECTOR ==
                 GDT_USER_DATA_SELECTOR + GDT_ENTRY_BYTES,
                 "SYSRETQ requires user CS immediately after SS");

  /* STAR[47:32] supplies kernel CS; SYSCALL adds 8 for kernel SS.
   * For SYSRETQ, STAR[63:48] is a base: SS = base + 8, CS = base + 16,
   * with RPL forced to 3. The base itself is not loaded in 64-bit mode. */
  uint64_t kernel_cs = GDT_KERNEL_CODE_SELECTOR;
  uint64_t sysret_base = GDT_USER_DATA_SELECTOR - GDT_ENTRY_BYTES;
  write_msr(IA32_STAR, (kernel_cs << STAR_KERNEL_CS_SHIFT) |
                       (sysret_base << STAR_SYSRET_BASE_SHIFT));

  write_msr(IA32_LSTAR, (uintptr_t)syscall_entry);
  write_msr(IA32_FMASK, SYSCALL_ENTRY_FLAGS_MASK);

  /* Publish the entry point and flag mask before enabling the instructions.
   * Preserve the NX and long-mode state established by paging_init. */
  write_msr(IA32_EFER, read_msr(IA32_EFER) | EFER_SCE);
}

void early_init(void)
{
  serial_init();
  klog("\nPyxis OS / Caelum\n");
}

void arch_init(struct boot_info *boot)
{
  struct cpu_local *cpu = cpu_bsp();
  gdt_init(&cpu->descriptors, cpu->double_fault_stack_top);
  cpu_install_local(cpu);
  idt_init();
  klog("x86_64: kernel GDT, IDT and double-fault IST installed; interrupts disabled\n");
  io_apic_prepare(boot);
  arch_clock_prepare(boot);
  paging_init(boot);
  arch_clock_init();
  apic_init();
  arch_clock_maintain();
  if (io_apic_init()) {
    ps2_keyboard_init();
  }
  arch_clock_maintain();
  arch_user_init();
  arch_syscall_init();
}
