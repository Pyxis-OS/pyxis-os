#include <arch/cpu.h>
#include <arch/amd/renoir_inventory.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <arch/debug.h>
#include <kernel/init.h>
#include <kernel/acpi.h>
#include <kernel/user/launch.h>
#include <kernel/boot.h>
#include <kernel/boot/options.h>
#include <kernel/boot_files.h>
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
#include <kernel/audio.h>
#include <kernel/task.h>
#include <kernel/usb/xhci.h>
#include <kernel/service/request.h>
#include <kernel/space.h>
#include <kernel/display.h>
#include <kernel-config.h>
#include "storage/block_registry.h"

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

  enum mm_result boot_files_result = boot_files_init(boot);
  if (boot_files_result != MM_OK) {
    panic("cannot retain boot files (error %u)", (unsigned)boot_files_result);
  }
  klog("boot files: original kernel=%zu bytes, mapped read-only\n", boot->kernel_file.size);
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
  audio_prepare(boot);
  virtio_blk_prepare(boot);
  block_prepare();
  gpt_prepare();
  acpi_prepare(boot);
  arch_clock_maintain();

  const struct boot_options *options = boot_options_get();
  if (options->display_inventory) {
    renoir_inventory(boot);
  }
  if (options->debug_checkpoint) {
    arch_debug_enable(boot);
  }
  display_init(boot, options->display_size, options->display_timing,
      options->display_timing_metrics);

  boot_start_cpus();
  system_info_init();
  arch_clock_maintain();

  space_init();
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
  audio_start();
  virtio_blk_start();
  gpt_start();
  npfs_start();
  acpi_start();
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
  klog("heap: TLSF pools=%zu bytes=%zu, alignment=16, live allocations=%zu; "
       "arena used=%zu retired=%zu bytes\n",
       heap.pools, heap.pool_bytes, heap.live_allocations, heap.arena_bytes,
       heap.retired_bytes);

  user_launch_initial(options);
  if (options->debug_checkpoint) {
    arch_debug_checkpoint();
  }
  arch_clock_maintain();
  klog("Caelum ready: starting preemptive userspace\n");
  task_schedule();
}
