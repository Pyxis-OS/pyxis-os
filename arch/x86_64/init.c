#include <arch/console.h>
#include <arch/descriptors.h>
#include <arch/init.h>
#include <arch/paging.h>
#include <kernel/log.h>

void early_init(void)
{
  serial_init();
  klog("\nPyxis OS / Caelum\n");
}

void arch_init(struct boot_info *boot)
{
  gdt_init();
  idt_init();
  klog("x86_64: kernel GDT, IDT and double-fault IST installed; interrupts disabled\n");
  paging_init(boot);
}
