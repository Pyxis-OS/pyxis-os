#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include <stdint.h>

#define MOUSE_BUTTON_LEFT (1u << 0)
#define MOUSE_BUTTON_RIGHT (1u << 1)
#define MOUSE_BUTTON_MIDDLE (1u << 2)

enum mouse_event_kind {
  MOUSE_EVENT_RELATIVE,
  MOUSE_EVENT_TOUCHPAD,
  MOUSE_EVENT_GUEST,
  MOUSE_EVENT_RESET,
};

enum touchpad_contact_flags {
  TOUCHPAD_CONTACT_TIP = 1u << 0,
  TOUCHPAD_CONTACT_CONFIDENCE = 1u << 1,
  TOUCHPAD_CONTACT_ID_VALID = 1u << 2,
};

struct touchpad_contact {
  int32_t x, y;
  uint32_t id;
  /* ID_VALID is required for Tip-active records; Confidence clear while Tip
   * is active is a palm rejection, independent of pressure or width. */
  uint8_t flags;
};

struct touchpad_sample {
  int32_t x, y;
  int32_t second_x, second_y;
  uint64_t observed_at, second_at;
  uint8_t pressure, width, fingers;
  uint8_t second_pressure;
  bool valid, width_valid, second_valid;
  /* For native profiles, valid/observed_at describe this frame; the legacy
   * pressure/width/count/position fields above are ignored. The
   * caller retains all records, including any Tip-off records, for this call.
   * Zero records in a complete valid frame confirms all-up. */
  const struct touchpad_contact *contacts;
  uint8_t contact_count;
  bool contacts_complete;
};

struct mouse_profile {
  bool absolute, has_guest, has_second;
  bool native_contacts; /* Complete Tip/Confidence/ID frames, without Z/W hints. */
  unsigned initial_touch_buttons;
  int32_t x_min, x_max, y_min, y_max;
  uint32_t x_resolution, y_resolution; /* Device counts per mm. */
};

/* Relative +dx/+dy follow display right/down; wheel pull is positive.
 * Absolute touch coordinates/pressure are device data, not screen positions.
 * Physical button snapshots are independent of contact validity and policy. */
struct mouse_event {
  enum mouse_event_kind kind;
  int32_t dx, dy, wheel;
  unsigned buttons; /* Relative device snapshot. */
  struct touchpad_sample touch;
  unsigned touch_buttons, guest_buttons;
  bool touch_buttons_valid, guest_buttons_valid;
};

bool mouse_available(void);
const struct mouse_profile *mouse_profile(void);
/* Bounded opt-in transport diagnostics, one BSP consumer. No device commands. */
void mouse_diagnose(uint64_t now);

/* One BSP consumer outside interrupt entry. Nonblocking and preserves IF.
 * RESET loses both logical sources on the shared transport. Contact rejection
 * never implies transport loss or releases physical switches. */
bool mouse_read_event(struct mouse_event *event);

#endif
