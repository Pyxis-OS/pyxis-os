/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_ACPI_GPIO_H
#define KERNEL_ACPI_GPIO_H
#include <stdint.h>
#include <kernel/gpio.h>
#include <kernel/mm/types.h>

struct acpi_hid_i2c_interrupt;

/* Snapshot storage capacities, independent of firmware name limits. */
#define ACPI_GPIO_PATH_CAPACITY 256

/* Owned INTC105F platform-GPIO values; no namespace objects or borrowed data.
 * Register offsets are relative to the sole current memory resource. The pin
 * is local to CONTROLLER; the group layout comes from its live _DSD. */
struct acpi_gpio_input {
  char controller[ACPI_GPIO_PATH_CAPACITY];
  char group[32];
  phys_addr_t physical;
  uint64_t bytes, uid;
  uint32_t pin, pin_count, stride, padcfg_offset, padown_offset, padown_shift;
};

/* ACPI worker, IF=1, after namespace initialization. One attempt per boot.
 * Captures the exact resource-source device, then fills a separately owned
 * native window with interrupts disabled. NULL means unsupported or refused.
 * Does not change firmware configuration or claim/enable an interrupt. */
const struct gpio_input *acpi_gpio_input_attach(
    const struct acpi_hid_i2c_interrupt *interrupt);
#endif
