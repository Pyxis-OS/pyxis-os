#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/init.h>
#include <kernel/user/launch.h>
#include <kernel/boot.h>
#include <kernel/initrd.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/net/interface.h>
#include <kernel/net/rtl8111.h>
#include <kernel/object/clock.h>
#include <kernel/object/system_info.h>
#include <kernel/panic.h>
#include <kernel/pci.h>
#include <kernel/gpt.h>
#include <kernel/fs/npfs.h>
#include <kernel/virtio/blk.h>
#include <kernel/virtio/pci.h>
#include <kernel/virtio/net.h>
#include <kernel/random.h>
#include <kernel/task.h>
#include <kernel/usb/xhci.h>
#include <kernel/service/request.h>
#include <kernel/space.h>
#include <kernel-config.h>

[[noreturn]] void kernel_init(const struct boot_info *boot)
{
  clock_init(boot);
  vm_init();
  if (!heap_init()) {
    panic("cannot initialize the TLSF heap");
  }
  klog("mm: kernel VM and heap ready\n");
  arch_clock_maintain();

  enum initrd_result archive_result = initrd_init(&boot->initrd);
  if (archive_result != INITRD_OK) {
    panic("cannot initialize boot archive (error %u)", (unsigned)archive_result);
  }
  klog("initrd: newc archive=%zu bytes, mapped read-only\n", boot->initrd.size);
  arch_clock_maintain();

  klog("PCI: discovery starting\n");
  pci_discover();
  klog("PCI: discovery complete\n");
  arch_clock_maintain();
#ifdef CONFIG_XHCI
  xhci_prepare(boot);
#else
  klog("xHCI: disabled at build time\n");
#endif
  virtio_fs_pci_prepare(boot);
  virtio_net_prepare(boot);
  rtl8111_prepare(boot);
  random_prepare(boot);
  virtio_blk_prepare(boot);
  gpt_prepare();
  arch_clock_maintain();

  boot_start_cpus();
  system_info_init();
  arch_clock_maintain();

  space_init_all(&boot->framebuffer);
  arch_clock_maintain();

  task_init();
  bsp_requests_init();
  klog("tasks: scheduler and BSP request queues ready\n");
  arch_clock_maintain();

#ifdef CONFIG_XHCI
  xhci_start();
#endif

  klog("devices: starting virtio, block and native filesystem workers\n");
  virtio_fs_pci_start();
  random_start();
  virtio_blk_start();
  gpt_start();
  npfs_start();
  klog("devices: workers started\n");
  arch_clock_maintain();

  enum mm_result result = net_init();
  if (result != MM_OK) {
    klog("net: cannot create worker (error %u); networking unavailable\n", (unsigned)result);
  }

  result = kernel_task_create(space_present_task, NULL);
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

  user_launch_initial(boot->command_line);
  arch_clock_maintain();
  klog("Caelum ready: starting preemptive userspace\n");
  task_schedule();
}
