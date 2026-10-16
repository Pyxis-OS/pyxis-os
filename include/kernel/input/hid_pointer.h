/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_INPUT_HID_POINTER_H
#define KERNEL_INPUT_HID_POINTER_H

#include <kernel/input.h>
#include <kernel/input/hid_touchpad.h>
#include <kernel/input/touchpad.h>

struct hid_pointer {
  struct input_source source;
  struct touchpad_state filter;
  struct hid_touchpad_profile profile;
  int32_t width_um, height_um;
  bool initial_buttons_pending;
};

/* Adapter-owned, retained storage. Sole BSP worker, IF=1; these functions
 * internally exclude input delivery only for short state/queue changes.
 * Geometry is descriptor-derived, rounded to micrometres, never fake Z/W.
 * Prepare only before attachment or after loss. Ready publishes availability
 * before any input report; the first real button snapshot establishes initial
 * holds. Fresh complete all-up is required after prepare/loss. */
bool hid_pointer_prepare(struct hid_pointer *pointer,
    const struct hid_touchpad_profile *profile);
void hid_pointer_ready(struct hid_pointer *pointer);
void hid_pointer_report(struct hid_pointer *pointer, const struct hid_touchpad_frame *frame);
void hid_pointer_lost(struct hid_pointer *pointer);

#endif
