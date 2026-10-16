/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_I2C_INTEL_LPSS_H
#define KERNEL_I2C_INTEL_LPSS_H
#include <kernel/acpi/hid_i2c.h>
#include <kernel/boot.h>

/* BSP/IF=0 before AP startup. One unique supported LPSS I2C0 only. Performs
 * checked power/halt/claim/BAR preparation; no I2C client transaction. */
void intel_lpss_i2c_prepare(const struct boot_info *boot);

/* ACPI worker, BSP/IF=0 after all AML evaluation. Copies these owned values,
 * matches the exact prepared companion and starts one descriptor-read worker.
 * No borrowed firmware pointer is retained; resources remain owned until reboot. */
void intel_lpss_i2c_attach(const struct acpi_hid_i2c_endpoint *endpoint,
    const struct acpi_hid_i2c_pci *companion);
#endif
