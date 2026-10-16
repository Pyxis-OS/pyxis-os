/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_GPIO_H
#define KERNEL_GPIO_H
#include <stdbool.h>

struct gpio_input;

/* BSP worker. A retained read-only attachment, valid until reboot. Returns
 * false if the hardware no longer exposes a usable input; HIGH is unchanged.
 * Reads the electrical level, independent of the client's active polarity.
 * No allocation, AML, pad configuration, ownership or interrupt writes. */
bool gpio_input_level(const struct gpio_input *input, bool *high);
#endif
