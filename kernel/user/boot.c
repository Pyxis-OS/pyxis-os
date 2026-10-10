#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/boot/options.h>
#include <kernel/keyboard.h>
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
  /* Keep boot image allocation/publication in its original BSP/IF=0 context.
   * Discovery waits above run preemptibly, without holding any input lock. */
  uint64_t flags = cpu_save_interrupts();
  user_launch_boot_init(options->init, &options->mount, options->install,
      options->default_config, options->remote_beacon);
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
