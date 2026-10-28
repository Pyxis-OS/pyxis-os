#include <arch/cpu.h>
#include <arch/amd/renoir_inventory.h>
#include <arch/intel/display_inventory.h>
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
#include <kernel/object/mount.h>
#include <kernel/log.h>
#include <kernel/i2c/intel_lpss.h>
#include <arch/intel_gpio.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/net/interface.h>
#include <kernel/net/rtl8111.h>
#include <kernel/object/clock.h>
#include <kernel/object/system_info.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/pci.h>
#include <kernel/gpt.h>
#include <kernel/fs/npfs.h>
#include <kernel/virtio/blk.h>
#include <kernel/virtio/pci.h>
#include <kernel/virtio/net.h>
#include <kernel/random.h>
#include <kernel/audio.h>
#include <kernel/wifi.h>
#include <kernel/task.h>
#include <kernel/usb/xhci.h>
#include <kernel/service/request.h>
#include <kernel/pci/assignment.h>
#include <kernel/space.h>
#include <kernel/display.h>
#include <kernel/debug.h>
#include <kernel-config.h>
#include "storage/block_registry.h"
#include <kernel/storage/nvme.h>

[[noreturn]] void kernel_init(const struct boot_info *boot)
{
  clock_init(boot);
  vm_init();
  if (!heap_init()) {
    panic("cannot initialize the TLSF heap");
  }
  kinfo("mm: kernel VM and heap ready\n");
  arch_clock_maintain();

  enum initrd_result archive_result = initrd_init(&boot->initrd);
  if (archive_result != INITRD_OK) {
    panic("cannot initialize boot archive (error %u)", (unsigned)archive_result);
  }
  kinfo("initrd: newc archive=%zu bytes, mapped read-only\n", boot->initrd.size);
  arch_clock_maintain();

  mount_set_boot_origin(&boot->origin);
  enum mm_result boot_files_result = boot_files_init(boot);
  if (boot_files_result != MM_OK) {
    panic("cannot retain boot files (error %u)", (unsigned)boot_files_result);
  }
  kinfo("boot files: original kernel=%zu bytes, mapped read-only\n", boot->kernel_file.size);
  arch_clock_maintain();

  kinfo("PCI: discovery starting\n");
  pci_discover();
  pci_assignment_prepare(boot);
  kinfo("PCI: discovery complete\n");
  arch_clock_maintain();
#ifdef CONFIG_XHCI
  xhci_prepare(boot);
#else
  kinfo("xHCI: disabled at build time\n");
#endif
  virtio_fs_pci_prepare(boot);
  virtio_net_prepare(boot);
  rtl8111_prepare(boot);
  random_prepare(boot);
  audio_prepare(boot);
  virtio_blk_prepare(boot);
  nvme_prepare(boot);
  block_prepare();
  gpt_prepare();
  intel_lpss_i2c_prepare(boot);
  intel_gpio_input_prepare(boot);
  acpi_prepare(boot);
  arch_clock_maintain();

  const struct boot_options *options = boot_options_get();
  if (options->display_inventory) {
    renoir_inventory(boot);
    intel_display_inventory(boot);
  }
  display_init(boot, options->display_size, options->display_timing,
      options->display_timing_metrics);
  debug_prepare(boot, options);
  /* Optional radio reservations follow the other boot device preparations. */
  wifi_prepare(boot);

  boot_start_cpus();
  arch_debug_inspect_finish();
  system_info_init();
  arch_clock_maintain();

  space_init();
  arch_clock_maintain();

  task_init();
  bsp_requests_init();
  process_reaper_init();
  kinfo("tasks: scheduler and BSP request queues ready\n");
  arch_clock_maintain();

#ifdef CONFIG_XHCI
  xhci_start();
#endif

  kinfo("devices: starting virtio, block and native filesystem workers\n");
  virtio_fs_pci_start();
  random_start();
  debug_start();
  audio_start();
  virtio_blk_start();
  nvme_start();
  gpt_start();
  npfs_start();
  intel_lpss_i2c_start();
  acpi_start();
  kinfo("devices: workers started\n");
  arch_clock_maintain();

  enum mm_result result = net_init();
  if (result != MM_OK) {
    kerror("net: cannot create worker (error %u); networking unavailable\n", (unsigned)result);
  }

  result = kernel_task_create(space_present_task, NULL);
  if (result != MM_OK) {
    panic("cannot create presentation task (error %u)", (unsigned)result);
  }

  /* Core service/presentation stacks take priority over an optional radio. */
  wifi_start();

  struct pmm_stats memory = pmm_get_stats();
  kinfo("PMM: total=%zu free=%zu allocated=%zu frames, metadata=%zu pages\n",
        memory.total_frames, memory.free_frames, memory.allocated_frames, memory.metadata_pages);

  struct vm_stats virtual = vm_get_stats(vm_kernel_space());
  struct heap_stats heap = heap_get_stats();

  kinfo("VM: total=%zu reserved=%zu backed=%zu pages, records=%zu/%u\n",
        virtual.total_pages, virtual.reserved_pages, virtual.backed_pages,
        virtual.range_records, VM_MAX_RANGES);
  kinfo("heap: TLSF pools=%zu bytes=%zu, alignment=16, live allocations=%zu; "
        "arena used=%zu retired=%zu bytes\n",
        heap.pools, heap.pool_bytes, heap.live_allocations, heap.arena_bytes,
        heap.retired_bytes);

  user_launch_initial(options);
  arch_clock_maintain();
  kinfo("Caelum ready: starting preemptive userspace\n");
  task_schedule();
}
