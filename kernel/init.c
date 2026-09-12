#include <arch/cpu.h>
#include <kernel/init.h>
#include <kernel/log.h>
#include <kernel/mm/pmm.h>

[[noreturn]] void kernel_init(const struct boot_info *boot)
{
  struct pmm_stats memory = pmm_get_stats();
  klog("PMM: total=%zu free=%zu allocated=%zu frames, metadata=%zu pages\n",
       memory.total_frames, memory.free_frames, memory.allocated_frames, memory.metadata_pages);
  klog("Caelum: memory takeover ready; image=%zu bytes\n", boot->kernel_size);
  cpu_halt();
}
