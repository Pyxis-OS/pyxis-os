/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_I2C_INTEL_LPSS_H
#define KERNEL_I2C_INTEL_LPSS_H
#include <kernel/acpi/hid_i2c.h>
#include <kernel/boot.h>
#include <kernel/gpio.h>

/* BSP/IF=0 before AP startup. One unique supported LPSS I2C0 only. Performs
 * checked power/halt/claim/BAR preparation; no I2C client transaction. */
void intel_lpss_i2c_prepare(const struct boot_info *boot);

/* BSP/IF=0 after task_init(), before scheduler startup. Prepares the one
 * descriptor worker; it parks until firmware publishes a matching endpoint. */
void intel_lpss_i2c_start(void);

/* ACPI worker, BSP/IF=0 after all AML evaluation. Copies these owned values,
 * matches the exact prepared companion and wakes the prepared transport worker.
 * No borrowed firmware pointer is retained; resources remain owned until reboot. */
void intel_lpss_i2c_attach(const struct acpi_hid_i2c_endpoint *endpoint,
    const struct acpi_hid_i2c_pci *companion, const struct gpio_input *interrupt);
#endif
