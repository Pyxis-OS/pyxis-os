/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ARCH_INTEL_GPIO_H
#define ARCH_INTEL_GPIO_H
#include <kernel/boot.h>
#include <kernel/gpio.h>

struct acpi_gpio_input;

/* BSP/IF=0 before AP startup. Retains memory exclusions and reserves three
 * fresh page slots: community identity, pad ownership and input configuration.
 * No GPIO address is guessed or accessed during preparation. */
void intel_gpio_input_prepare(const struct boot_info *boot);

/* BSP/IF=0 after AML capture. One fill attempt, without remapping/reusing any
 * slot. Validates firmware layout against REVID/PADBAR and copies SNAPSHOT.
 * All mappings/storage, including partial failures, remain until reboot. */
const struct gpio_input *intel_gpio_input_attach(const struct acpi_gpio_input *snapshot);
#endif
