/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_INPUT_HID_TOUCHPAD_H
#define KERNEL_INPUT_HID_TOUCHPAD_H

#include <kernel/input/hid_layout.h>

/* Prepared parallel-contact capacity, not an HID report-ID or finger limit. */
#define HID_TOUCHPAD_CONTACTS 5

enum hid_touchpad_button {
  HID_TOUCHPAD_PRIMARY = 1u << 0,
  HID_TOUCHPAD_SECONDARY = 1u << 1,
};

struct hid_touchpad_contact_layout {
  struct hid_layout_field tip, confidence, id, x, y;
};

struct hid_touchpad_profile {
  struct hid_touchpad_contact_layout contacts[HID_TOUCHPAD_CONTACTS];
  struct hid_layout_field count, scan_time;
  /* Descriptor usages 1/2/3: integrated primary, external primary/secondary. */
  struct hid_layout_field buttons[3];
  struct hid_layout_field count_maximum, button_type;
  /* Complete HID image bytes, including report ID when numbered, excluding
   * the I2C length. Coordinates retain raw logical/physical ranges and units;
   * no screen orientation, pressure, width or filtering is invented. */
  size_t report_bytes;
  uint8_t report_id, slot_count;
  bool numbered;
};

struct hid_touchpad_contact {
  uint32_t id;
  int32_t x, y;
  bool tip, confidence;
};

struct hid_touchpad_frame {
  struct hid_touchpad_contact contacts[HID_TOUCHPAD_CONTACTS];
  uint16_t scan_time;
  /* Count includes reported UP records; entries beyond it are unused. */
  uint8_t count;
  unsigned buttons;
};

/* Decode one complete admitted parallel report, without retaining state.
 * Caller checks the device's Contact Count Maximum <= slot_count before use.
 * All counted records, including UP records, must have distinct IDs and valid
 * coordinates. Confidence does not suppress buttons or contact records.
 * Button usages 1/2 are ORed into primary; usage 3 is secondary.
 * Failure clears FRAME; REASON, if supplied, receives a static explanation.
 * Outputs may not alias PROFILE, REPORT or each other. */
bool hid_touchpad_decode(const struct hid_touchpad_profile *profile,
    const uint8_t *report, size_t bytes, struct hid_touchpad_frame *frame,
    const char **reason);

#endif
