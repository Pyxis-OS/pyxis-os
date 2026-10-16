/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_INPUT_HID_CLICKPAD_H
#define KERNEL_INPUT_HID_CLICKPAD_H

#include <kernel/input/hid_touchpad.h>
#include <kernel/mouse.h>

enum hid_clickpad_role {
  HID_CLICKPAD_UNUSED,
  HID_CLICKPAD_MAIN,
  HID_CLICKPAD_BUTTON_ZONE,
};

struct hid_clickpad_contact {
  uint32_t id;
  enum hid_clickpad_role role;
};

/* HID adapter-owned retained state, with the same sole BSP owner as its input
 * source. Contact roles last until Tip-off or absence from a complete frame;
 * the selected logical button lasts until the physical switch releases. */
struct hid_clickpad {
  struct hid_clickpad_contact contacts[HID_TOUCHPAD_CONTACTS];
  int32_t width_um, height_um;
  uint32_t selected_button;
  bool enabled, physical_left;
};

/* Enable only for an identified single-switch HID clickpad. Coordinates are
 * micrometres in 0..width/height, increasing Y upward. Reinitialize after loss
 * or recovery, before the next initial-button snapshot. */
void hid_clickpad_init(struct hid_clickpad *state, bool enabled,
    int32_t width_um, int32_t height_um);

/* Complete decoded frames only: bounded count, unique IDs, valid coordinates,
 * and ID_VALID on every Tip. Roles still track while trusted is false, but a
 * new physical press then falls back to primary. Nonconfident arrivals remain
 * main-area contacts for that lifetime; any nonconfident Tip also forces a new
 * press to primary. Zone contacts use their current side before physical down.
 * Main contacts and zone contacts that leave the bottom quarter stay main until
 * lift. Only BUTTON_ZONE flags are modified;
 * the shared filter must observe all Tips/Confidence before excluding zones
 * from motion. Other physical button bits pass through unchanged. */
uint32_t hid_clickpad_report(struct hid_clickpad *state,
    struct touchpad_contact *contacts, unsigned count, uint32_t raw_buttons,
    bool trusted);

#endif
