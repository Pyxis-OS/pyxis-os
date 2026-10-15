/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_ACPI_HID_I2C_H
#define KERNEL_ACPI_HID_I2C_H
#include <stdbool.h>
#include <stdint.h>

/* Internal snapshot capacity, not a firmware or ABI path limit. */
#define ACPI_HID_I2C_PATH_CAPACITY 256

enum acpi_hid_i2c_state {
  ACPI_HID_I2C_PENDING,
  ACPI_HID_I2C_ABSENT,
  ACPI_HID_I2C_REFUSED,
  ACPI_HID_I2C_READY,
};

struct acpi_hid_i2c_source {
  char path[ACPI_HID_I2C_PATH_CAPACITY];
  bool index_present;
  uint8_t index;
};

enum acpi_hid_i2c_interrupt_kind {
  ACPI_HID_I2C_GPIO,
  ACPI_HID_I2C_EXTENDED_IRQ,
};

/* Scalar flags retain ACPI resource encodings: consumer=1, level=0/edge=1,
 * high=0/low=1/both=2, exclusive=0/shared=1, wake-capable=1. A source-bearing
 * extended IRQ is not a direct GSI. GPIO pins are relative to SOURCE. */
struct acpi_hid_i2c_interrupt {
  enum acpi_hid_i2c_interrupt_kind kind;
  struct acpi_hid_i2c_source source;
  uint32_t number;
  uint8_t direction, triggering, polarity, sharing, wake;
  uint8_t pull;
  uint16_t drive_strength, debounce;
};

struct acpi_hid_i2c_endpoint {
  char path[ACPI_HID_I2C_PATH_CAPACITY];
  uint32_t status;
  struct acpi_hid_i2c_source controller;
  uint32_t speed_hz;
  uint16_t address, descriptor_register;
  /* ACPI encodings: controller-initiated=0, consumer=1, exclusive=0/shared=1,
   * seven-bit=0/ten-bit=1. No controller claim or usable power state implied. */
  uint8_t mode, direction, sharing, addressing_mode;
  struct acpi_hid_i2c_interrupt interrupt;
};

/* BSP, IF=0. Copies the immutable, owned ELAN06FA/PNP0C50 result only when
 * READY. No uACPI pointers escape; before discovery completes returns PENDING.
 * Other states leave ENDPOINT untouched. No transfers or resource claims. */
enum acpi_hid_i2c_state acpi_hid_i2c_snapshot(struct acpi_hid_i2c_endpoint *endpoint);
#endif
