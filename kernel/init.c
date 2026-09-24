#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/init.h>
#include <kernel/user/launch.h>
#include <kernel/boot.h>
#include <kernel/initrd.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/object/clock.h>
#include <kernel/panic.h>
#include <kernel/pci.h>
#include <kernel/task.h>
#include <kernel/space.h>

[[noreturn]] void kernel_init(const struct boot_info *boot)
{
  clock_init(boot);
  vm_init();
  if (!heap_init()) {
    panic("cannot initialize the TLSF heap");
  }

  enum initrd_result archive_result = initrd_init(&boot->initrd);
  if (archive_result != INITRD_OK) {
    panic("cannot initialize boot archive (error %u)", (unsigned)archive_result);
  }
  klog("initrd: newc archive=%zu bytes, mapped read-only\n", boot->initrd.size);

  pci_discover();

  boot_start_cpus();

  space_init_all(&boot->framebuffer);

  task_init();

  enum mm_result result = kernel_task_create(space_present_task, NULL);
  if (result != MM_OK) {
    panic("cannot create presentation task (error %u)", (unsigned)result);
  }

  struct pmm_stats memory = pmm_get_stats();
  klog("PMM: total=%zu free=%zu allocated=%zu frames, metadata=%zu pages\n",
       memory.total_frames, memory.free_frames, memory.allocated_frames, memory.metadata_pages);

  struct vm_stats virtual = vm_get_stats(vm_kernel_space());
  struct heap_stats heap = heap_get_stats();

  klog("VM: total=%zu reserved=%zu backed=%zu pages, records=%zu/%u\n",
       virtual.total_pages, virtual.reserved_pages, virtual.backed_pages,
       virtual.range_records, VM_MAX_RANGES);
  klog("heap: TLSF pools=%zu bytes=%zu, alignment=16, live allocations=%zu\n",
       heap.pools, heap.pool_bytes, heap.live_allocations);

  user_launch_initial();
  klog("Caelum ready: starting preemptive userspace\n");
  task_schedule();
}
