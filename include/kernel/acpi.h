#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H
#include <abi/syscall.h>
#include <kernel/boot.h>
#include <kernel/service/request.h>

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

#endif
