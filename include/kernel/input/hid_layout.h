/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_INPUT_HID_LAYOUT_H
#define KERNEL_INPUT_HID_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum hid_layout_type {
  HID_LAYOUT_INPUT,
  HID_LAYOUT_OUTPUT,
  HID_LAYOUT_FEATURE,
  HID_LAYOUT_TYPES,
};

struct hid_report_layout {
  /* Data bits exclude the transport length and any report-ID prefix. Zero
   * denotes an absent report. The caller retains this storage off-stack. */
  uint32_t bits[HID_LAYOUT_TYPES][256];
  bool numbered[HID_LAYOUT_TYPES];
  bool mode_present;
  uint8_t mode_id, mode_width;
  uint32_t mode_bit;
};

/* Allocation-free layout admission, without decoding contacts or pointers.
 * Fixed parser budgets are 16 nested globals/collections and 32 local usage
 * spans. Long items, delimiters and reserved items are unsupported.
 * Input Mode is one Data/Variable/Absolute feature in the Digitizers
 * Configuration application collection, with an unsigned scalar range
 * admitting 3 and a width of 2..32 bits.
 * Failure clears LAYOUT; REASON, if supplied, receives a static explanation.
 * LAYOUT and REASON may not alias the descriptor or each other. */
bool hid_layout_parse(struct hid_report_layout *layout, const uint8_t *descriptor,
    size_t bytes, const char **reason);

#endif
