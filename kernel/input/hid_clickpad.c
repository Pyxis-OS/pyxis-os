/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/input/hid_clickpad.h>

#define HID_CLICKPAD_BOTTOM_DIVISOR 4
#define HID_CLICKPAD_RIGHT_DIVISOR 2

void hid_clickpad_init(struct hid_clickpad *state, bool enabled,
    int32_t width_um, int32_t height_um)
{
  *state = (struct hid_clickpad){
    .width_um = width_um,
    .height_um = height_um,
    .enabled = enabled && width_um > 0 && height_um > 0,
  };
}

static bool bottom_zone(const struct hid_clickpad *state, int32_t y)
{
  return (int64_t)y * HID_CLICKPAD_BOTTOM_DIVISOR <= state->height_um;
}

static bool contact_present(const struct touchpad_contact *contacts,
    unsigned count, uint32_t id)
{
  for (unsigned i = 0; i < count; ++i) {
    if ((contacts[i].flags & (TOUCHPAD_CONTACT_TIP | TOUCHPAD_CONTACT_ID_VALID)) ==
        (TOUCHPAD_CONTACT_TIP | TOUCHPAD_CONTACT_ID_VALID) && contacts[i].id == id) {
      return true;
    }
  }
  return false;
}

static struct hid_clickpad_contact *contact_role(struct hid_clickpad *state,
    uint32_t id)
{
  struct hid_clickpad_contact *unused = NULL;
  for (unsigned i = 0; i < HID_TOUCHPAD_CONTACTS; ++i) {
    struct hid_clickpad_contact *contact = &state->contacts[i];
    if (contact->role != HID_CLICKPAD_UNUSED && contact->id == id) {
      return contact;
    }
    if (contact->role == HID_CLICKPAD_UNUSED && !unused) {
      unused = contact;
    }
  }
  return unused;
}

uint32_t hid_clickpad_report(struct hid_clickpad *state,
    struct touchpad_contact *contacts, unsigned count, uint32_t raw_buttons,
    bool trusted)
{
  bool complete = count <= HID_TOUCHPAD_CONTACTS && (!count || contacts);
  if (complete) {
    for (unsigned i = 0; i < count; ++i) {
      contacts[i].flags &= ~TOUCHPAD_CONTACT_BUTTON_ZONE;
    }
  }
  if (!state->enabled) {
    return raw_buttons;
  }

  unsigned main_contacts = 0;
  bool left_zone = false, right_zone = false;
  if (complete) {
    for (unsigned i = 0; i < HID_TOUCHPAD_CONTACTS; ++i) {
      struct hid_clickpad_contact *role = &state->contacts[i];
      if (role->role != HID_CLICKPAD_UNUSED &&
          !contact_present(contacts, count, role->id)) {
        *role = (struct hid_clickpad_contact){0};
      }
    }
    for (unsigned i = 0; i < count; ++i) {
      struct touchpad_contact *contact = &contacts[i];
      if (!(contact->flags & TOUCHPAD_CONTACT_TIP)) {
        continue;
      }
      if (!(contact->flags & TOUCHPAD_CONTACT_ID_VALID)) {
        trusted = false;
        continue;
      }
      struct hid_clickpad_contact *role = contact_role(state, contact->id);
      if (!role) {
        trusted = false;
        continue;
      }
      bool confident = contact->flags & TOUCHPAD_CONTACT_CONFIDENCE;
      if (!confident) {
        trusted = false;
      }
      bool bottom = bottom_zone(state, contact->y);
      if (role->role == HID_CLICKPAD_UNUSED) {
        *role = (struct hid_clickpad_contact){.id = contact->id,
            .role = confident && bottom ? HID_CLICKPAD_BUTTON_ZONE : HID_CLICKPAD_MAIN};
      } else if (role->role == HID_CLICKPAD_BUTTON_ZONE && !bottom) {
        /* Leaving the button area ends its role until this contact lifts. */
        role->role = HID_CLICKPAD_MAIN;
      }
      if (role->role == HID_CLICKPAD_BUTTON_ZONE) {
        contact->flags |= TOUCHPAD_CONTACT_BUTTON_ZONE;
        if (confident) {
          if ((int64_t)contact->x * HID_CLICKPAD_RIGHT_DIVISOR >= state->width_um) {
            right_zone = true;
          } else {
            left_zone = true;
          }
        }
      } else if (confident) {
        ++main_contacts;
      }
    }
  } else {
    trusted = false;
  }

  bool physical_left = raw_buttons & MOUSE_BUTTON_LEFT;
  if (physical_left && !state->physical_left) {
    state->selected_button = MOUSE_BUTTON_LEFT;
    /* A zone thumb takes priority over the main-area two-finger mapping. */
    if (trusted && (right_zone || (!left_zone && main_contacts == 2))) {
      state->selected_button = MOUSE_BUTTON_RIGHT;
    }
  } else if (!physical_left) {
    state->selected_button = 0;
  }
  state->physical_left = physical_left;
  return (raw_buttons & ~MOUSE_BUTTON_LEFT) | state->selected_button;
}
