#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H
#include <abi/syscall.h>
#include <abi/system_info.h>
#include <kernel/boot.h>
#include <kernel/service/request.h>
#include <stdint.h>

/* BSP bootstrap after VM and heap initialization, before AP startup. Copies
 * the RSDP address and firmware memory map and reserves the firmware mapping
 * window. Failure leaves ACPI unavailable; it never stops boot. */
void acpi_prepare(const struct boot_info *boot);

/* BSP, IF=0, after task_init(). Creates the single ACPI worker, which owns
 * uACPI, AML execution, the SCI and deferred ACPI work. */
void acpi_start(void);

/* SCI interrupt entry on the BSP, IF=0. Masks the input and wakes the worker;
 * no uACPI code runs in interrupt entry. */
void acpi_interrupt(void);

enum acpi_power_action {
  ACPI_POWER_OFF,
  ACPI_POWER_RESTART,
};

/* BSP_SERVICE_POWER. The ACPI worker owns a forwarded request and completes it
 * only on failure, with status set; success powers off or resets instead. */
struct acpi_power_request {
  struct bsp_request request;
  enum acpi_power_action action;
  enum call_status status;
};

/* BSP executor, IF=0. Hands the request to the ACPI worker. Without a running
 * worker it completes at once with UNAVAILABLE; while another power request
 * runs it completes with BUSY. */
void acpi_power_forward(struct acpi_power_request *request);

/* BSP, IF=0. A power operation chosen at a local keyboard, with the power
 * button's physical-access authority. Returns UNAVAILABLE without a running
 * worker and BUSY while another operation is pending or running; otherwise
 * OK, and the worker runs it. Success never returns. */
enum call_status acpi_power_local(enum acpi_power_action action);

/* BSP, IF=0. True once a local operation has failed and the system stayed
 * up, with its status; each failure is reported once. */
bool acpi_power_local_failed(enum call_status *status);

/* The batteries' combined charge, polled every few seconds. Without a battery,
 * or before the first reading, present is false. */
struct acpi_battery_status {
  bool present;
  uint8_t percent;
  bool charging;
  bool ac_online;
};

/* BSP, IF=0. The latest reading. */
struct acpi_battery_status acpi_battery_status(void);

/* BSP, IF=0. The latest poll for system_info: AC state and battery count, and
 * battery INDEX's record, false past the count. All zero before the first
 * poll or without ACPI. */
struct system_info_power acpi_power_state(void);
bool acpi_battery_read(uint64_t index, struct system_info_battery *battery);

#endif
