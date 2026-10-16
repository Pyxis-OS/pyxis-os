/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_INPUT_HID_I2C_H
#define KERNEL_INPUT_HID_I2C_H
#include <kernel/acpi/hid_i2c.h>
#include <kernel/i2c/designware.h>
#include <kernel/gpio.h>

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
/* Sole BSP worker. Owns boot-retained buffers and returns only on refusal or
 * terminal transport failure. GPIO is a read-only, firmware-derived input.
 * No pointer events or client recovery loop; task 4 must add bounded recovery. */
void hid_i2c_poll(struct dw_i2c_master *master,
    const struct acpi_hid_i2c_endpoint *endpoint,
    const struct hid_i2c_descriptor *descriptor, const struct gpio_input *interrupt);
#endif
