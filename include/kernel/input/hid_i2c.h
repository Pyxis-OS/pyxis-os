/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_INPUT_HID_I2C_H
#define KERNEL_INPUT_HID_I2C_H
#include <kernel/acpi/hid_i2c.h>
#include <kernel/i2c/designware.h>

struct hid_i2c_descriptor {
  uint16_t report_bytes, report_register, input_register, input_bytes;
  uint16_t output_register, output_bytes, command_register, data_register;
  uint16_t vendor, product, version;
};

/* Sole BSP controller worker, IF=1, serialized with all transfers. Reads only
 * the firmware-selected descriptor register; no SET_POWER, RESET or input.
 * Failure leaves DESCRIPTOR untouched, with a bounded diagnostic. */
bool hid_i2c_descriptor_read(struct dw_i2c_master *master,
    const struct acpi_hid_i2c_endpoint *endpoint, struct hid_i2c_descriptor *descriptor);
#endif
