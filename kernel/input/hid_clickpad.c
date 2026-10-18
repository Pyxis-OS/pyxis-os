/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/input/hid_clickpad.h>
#include <kernel-config.h>

#define HID_CLICKPAD_BOTTOM_DIVISOR 4
#define HID_CLICKPAD_RIGHT_DIVISOR 2
#define HID_CLICKPAD_UM_PER_MM UINT64_C(1000)
#define HID_CLICKPAD_NS_PER_MS UINT64_C(1000000)

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

static bool moved_past(int32_t origin_x, int32_t origin_y,
    const struct touchpad_contact *contact, uint64_t millimetres)
{
  /* Complete frames have coordinates within the positive int32 pad spans. */
  int64_t dx = (int64_t)contact->x - origin_x;
  int64_t dy = (int64_t)contact->y - origin_y;
  uint64_t limit = millimetres * HID_CLICKPAD_UM_PER_MM;
  return (uint64_t)(dx * dx) + (uint64_t)(dy * dy) > limit * limit;
}

static bool stationary_pair(const struct hid_clickpad_contact *first,
    const struct hid_clickpad_contact *second, uint64_t now)
{
  if (!first->history_valid || !second->history_valid || !first->still || !second->still) {
    return false;
  }
  uint64_t early = first->touchdown_at < second->touchdown_at ?
      first->touchdown_at : second->touchdown_at;
  uint64_t late = first->touchdown_at > second->touchdown_at ?
      first->touchdown_at : second->touchdown_at;
  return now >= late && late - early <= CONFIG_TOUCHPAD_CLICK_PAIR_MS * HID_CLICKPAD_NS_PER_MS &&
      now - late <= CONFIG_TOUCHPAD_CLICK_PRESS_MS * HID_CLICKPAD_NS_PER_MS;
}

uint32_t hid_clickpad_report(struct hid_clickpad *state,
    struct touchpad_contact *contacts, unsigned count, uint32_t raw_buttons,
    bool trusted, uint64_t now)
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
  unsigned tips = 0;
  struct hid_clickpad_contact *pair[2] = {0};
  const struct touchpad_contact *pair_contacts[2] = {0};
  bool left_zone = false, right_zone = false;
  bool physical_left = raw_buttons & MOUSE_BUTTON_LEFT;
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
            .role = confident && bottom ? HID_CLICKPAD_BUTTON_ZONE : HID_CLICKPAD_MAIN,
            .origin_x = contact->x, .origin_y = contact->y, .touchdown_at = now,
            .history_valid = trusted && !physical_left, .still = true};
      } else if (role->role == HID_CLICKPAD_BUTTON_ZONE && !bottom) {
        /* Leaving the button area ends its role until this contact lifts. */
        role->role = HID_CLICKPAD_MAIN;
      }
      role->history_valid &= trusted && now >= role->touchdown_at;
      role->still &= !moved_past(role->origin_x, role->origin_y, contact,
          CONFIG_TOUCHPAD_CLICK_STILL_MM);
      if (role->role == HID_CLICKPAD_BUTTON_ZONE && trusted && !physical_left &&
          !role->press_seen && moved_past(role->origin_x, role->origin_y, contact,
              CONFIG_TOUCHPAD_BUTTON_MOVE_MM)) {
        role->role = HID_CLICKPAD_MAIN;
      }
      role->press_seen |= physical_left;
      if (tips < 2) {
        pair[tips] = role;
        pair_contacts[tips] = contact;
      }
      ++tips;
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

  if (!trusted) {
    /* A late untrusted contact also invalidates the earlier contacts' history. */
    for (unsigned i = 0; i < HID_TOUCHPAD_CONTACTS; ++i) {
      state->contacts[i].history_valid = false;
    }
  }
  if (!trusted || tips != 2 || !pair[0]->history_valid || !pair[1]->history_valid) {
    state->secondary_pair_valid = false;
  } else if (state->secondary_pair_valid) {
    const struct hid_clickpad_pair_contact *first = &state->secondary_pair[0];
    const struct hid_clickpad_pair_contact *second = &state->secondary_pair[1];
    if (pair[0]->id != first->id) {
      first = &state->secondary_pair[1];
      second = &state->secondary_pair[0];
    }
    if (pair[0]->id != first->id || pair[1]->id != second->id ||
        moved_past(first->x, first->y, pair_contacts[0], CONFIG_TOUCHPAD_CLICK_HOLD_MM) ||
        moved_past(second->x, second->y, pair_contacts[1], CONFIG_TOUCHPAD_CLICK_HOLD_MM)) {
      state->secondary_pair_valid = false;
    }
  }
  if (physical_left && !state->physical_left) {
    state->selected_button = MOUSE_BUTTON_LEFT;
    bool recent_pair = tips == 2 && stationary_pair(pair[0], pair[1], now);
    if (trusted && (right_zone || (!left_zone && main_contacts == 2) ||
        (left_zone && (recent_pair || state->secondary_pair_valid)))) {
      state->selected_button = MOUSE_BUTTON_RIGHT;
      if (left_zone && recent_pair && !state->secondary_pair_valid) {
        for (unsigned i = 0; i < 2; ++i) {
          state->secondary_pair[i] = (struct hid_clickpad_pair_contact){
            .id = pair[i]->id, .x = pair_contacts[i]->x, .y = pair_contacts[i]->y,
          };
        }
        state->secondary_pair_valid = true;
      }
    }
  } else if (!physical_left) {
    state->selected_button = 0;
  }
  state->physical_left = physical_left;
  return (raw_buttons & ~MOUSE_BUTTON_LEFT) | state->selected_button;
}
