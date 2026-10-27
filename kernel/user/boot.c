#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/boot/options.h>
#include <kernel/keyboard.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/net/log_udp.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user/launch.h>
#include "../usb/core.h"

#define BOOT_INPUT_POLL_MS 10

static bool initial_input_pending(void)
{
  struct system_info_usb usb;
  usb_inventory_read(&usb);
  return !keyboard_available() && usb.state == SYSTEM_INFO_USB_INITIALIZING;
}

static void launch_initial(void *argument)
{
  const struct boot_options *options = argument;
  while (initial_input_pending()) {
    kernel_task_sleep_until(task_deadline_after_ms(BOOT_INPUT_POLL_MS));
  }
  /* A delayed USB boot launch runs after scheduler publication. Retain its
   * complete preparation through publication; temporary contention cannot make
   * the one-shot boot-init path fail. The immediate path has bootstrap proof. */
  struct vm_kernel_owner owner = {0};
  bool runtime = vm_kernel_runtime();
  if (runtime) {
    vm_kernel_owner_cleanup(&owner);
  }
  uint64_t flags = cpu_save_interrupts();
  user_launch_boot_init(options->init, &options->mount, options->pool_partition,
      options->pool_malformed, options->install, options->default_config,
      options->remote_beacon);
  if (runtime) {
    vm_kernel_owner_release(&owner);
  }
  cpu_restore_interrupts(flags);
}

void user_launch_initial(const struct boot_options *options)
{
  KASSERT(arch_cpu_index() == 0);
  if (options->log_udp) {
    net_log_udp_enable();
  }
  /* A USB-only shell must not observe "unavailable" before boot enumeration.
   * Do not wait for a future attachment after initial discovery has finished. */
  if (!initial_input_pending()) {
    launch_initial((void *)options);
    return;
  }
  enum mm_result result = kernel_task_create(launch_initial, (void *)options);
  if (result != MM_OK) {
    panic("cannot create initial input discovery waiter (error %u)", (unsigned)result);
  }
}
