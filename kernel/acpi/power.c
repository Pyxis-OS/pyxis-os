#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/reset.h>
#include <kernel/fs/npfs.h>
#include <kernel/log.h>
#include <kernel/task.h>
#include <uacpi/sleep.h>
#include <uacpi/status.h>
#include "host.h"

/* A reset register write that has not reset the machine by then has failed. */
#define RESET_WAIT_NS UINT64_C(1000000000)

/* Returns only if the firmware did not power off. The pools are already
 * flushed, so a failing _PTS does not stop power-off, as on Linux; entry
 * itself refuses when _S5 gave no valid sleep type. */
static enum call_status power_off(void)
{
  uacpi_status status = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
  if (status != UACPI_STATUS_OK) {
    klog("power: S5 preparation failed: %s; entering S5 anyway\n",
         uacpi_status_to_string(status));
  }
  status = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
  klog("power: firmware did not power off: %s\n", uacpi_status_to_string(status));
  return CALL_UNAVAILABLE;
}

/* The FADT reset register first, then the architecture fallback. */
[[noreturn]] static void restart(void)
{
  uacpi_status status = uacpi_reboot();
  if (status == UACPI_STATUS_OK) {
    uint64_t end = arch_monotonic_ns() + RESET_WAIT_NS;
    while (arch_monotonic_ns() < end) {
      __asm__ volatile("pause");
    }
    klog("power: reset register did not restart; using the fallback\n");
  } else {
    klog("power: no usable reset register (%s); using the fallback\n",
         uacpi_status_to_string(status));
  }
  arch_reset_fallback();
}

enum call_status acpi_power_run(enum acpi_power_action action)
{
  acpi_require_worker();
  bool off = action == ACPI_POWER_OFF;
  klog("power: flushing pools; %s\n", off ? "powering off" : "restarting");

  uint64_t flags = cpu_save_interrupts();
  task_user_hold();
  cpu_restore_interrupts(flags);

  enum call_status status = npfs_shutdown_flush();
  if (status == CALL_OK) {
    if (!off) {
      restart();
    }
    status = power_off();
    flags = cpu_save_interrupts();
    npfs_shutdown_cancel();
    cpu_restore_interrupts(flags);
  }

  klog("power: %s failed (status %u); the system stays up\n",
       off ? "power-off" : "restart", (unsigned)status);
  flags = cpu_save_interrupts();
  task_user_release();
  cpu_restore_interrupts(flags);
  return status;
}
