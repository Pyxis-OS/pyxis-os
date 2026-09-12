#include <arch/console.h>
#include <arch/descriptors.h>
#include <arch/init.h>
#include <kernel/log.h>

void early_init(void)
{
  serial_init();
  klog("\nPyxis OS / Caelum\n");
}

void arch_init(struct boot_info *boot)
{
  (void)boot;
  gdt_init();
  idt_init();
  klog("x86_64: kernel GDT, IDT and double-fault IST installed; interrupts disabled\n");
}
