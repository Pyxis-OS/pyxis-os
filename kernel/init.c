#include <arch/cpu.h>
#include <kernel/init.h>
#include <kernel/log.h>

[[noreturn]] void kernel_init(const struct boot_info *boot)
{
  klog("Caelum: boot foundation ready; image=%zu bytes\n", boot->kernel_size);
  cpu_halt();
}
