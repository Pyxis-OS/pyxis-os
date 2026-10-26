#ifndef ABI_GAMEPAD_H
#define ABI_GAMEPAD_H

#include <abi/message.h>

#define GAMEPAD_RIGHT_INPUT (UINT64_C(1) << 0)
#define GAMEPAD_ACQUIRE UINT64_C(1)
#define GAMEPAD_READ UINT64_C(2)
#define GAMEPAD_RELEASE UINT64_C(3)
#define GAMEPAD_CATALOG UINT64_C(4)
#define GAMEPAD_INFO UINT64_C(5)
#define GAMEPAD_STATE UINT64_C(6)
#define GAMEPAD_READ_POLL (UINT64_C(1) << 0)
#define GAMEPAD_EVENT_FOCUSED (UINT32_C(1) << 0)
#define GAMEPAD_EVENT_VALID (UINT32_C(1) << 1)
#define GAMEPAD_PROFILE_DUALSENSE_EDGE_USB 1
#define GAMEPAD_DEVICE_CAPACITY 8
#define GAMEPAD_AXIS_COUNT 6
#define GAMEPAD_HAT_NEUTRAL 8

/* Edge profile: raw unsigned bytes, no kernel deadzone or trigger synthesis. */
enum gamepad_axis {
  GAMEPAD_AXIS_LEFT_X, GAMEPAD_AXIS_LEFT_Y,
  GAMEPAD_AXIS_RIGHT_X, GAMEPAD_AXIS_RIGHT_Y,
  GAMEPAD_AXIS_LEFT_TRIGGER, GAMEPAD_AXIS_RIGHT_TRIGGER,
};

#define GAMEPAD_BUTTON_SQUARE (UINT32_C(1) << 0)
#define GAMEPAD_BUTTON_CROSS (UINT32_C(1) << 1)
#define GAMEPAD_BUTTON_CIRCLE (UINT32_C(1) << 2)
#define GAMEPAD_BUTTON_TRIANGLE (UINT32_C(1) << 3)
#define GAMEPAD_BUTTON_L1 (UINT32_C(1) << 4)
#define GAMEPAD_BUTTON_R1 (UINT32_C(1) << 5)
#define GAMEPAD_BUTTON_L2 (UINT32_C(1) << 6)
#define GAMEPAD_BUTTON_R2 (UINT32_C(1) << 7)
#define GAMEPAD_BUTTON_CREATE (UINT32_C(1) << 8)
#define GAMEPAD_BUTTON_OPTIONS (UINT32_C(1) << 9)
#define GAMEPAD_BUTTON_L3 (UINT32_C(1) << 10)
#define GAMEPAD_BUTTON_R3 (UINT32_C(1) << 11)
#define GAMEPAD_BUTTON_PS (UINT32_C(1) << 12)
#define GAMEPAD_BUTTON_TOUCHPAD (UINT32_C(1) << 13)
#define GAMEPAD_BUTTON_MIC (UINT32_C(1) << 14)
#define GAMEPAD_BUTTON_LEFT_FUNCTION (UINT32_C(1) << 15)
#define GAMEPAD_BUTTON_RIGHT_FUNCTION (UINT32_C(1) << 16)
#define GAMEPAD_BUTTON_LEFT_PADDLE (UINT32_C(1) << 17)
#define GAMEPAD_BUTTON_RIGHT_PADDLE (UINT32_C(1) << 18)

struct gamepad_state {
  uint32_t buttons;
  uint8_t axes[GAMEPAD_AXIS_COUNT];
  uint8_t hat; /* 0..7 clockwise from up; 8 neutral. */
  uint8_t reserved; /* Zero. */
};

enum gamepad_event_type {
  GAMEPAD_INPUT, GAMEPAD_RESET, GAMEPAD_ATTACHED, GAMEPAD_REMOVED,
  GAMEPAD_FOCUS_GAINED, GAMEPAD_FOCUS_LOST,
};

struct gamepad_event {
  uint64_t instance, sequence, catalog_generation;
  uint32_t type, flags;
  struct gamepad_state state;
  uint32_t reserved;
};

struct gamepad_info {
  uint64_t instance;
  uint16_t vendor, product, version, profile;
  char name[48]; /* NUL-terminated trusted profile name, not USB serial identity. */
};

struct gamepad_catalog {
  uint64_t generation;
  uint32_t count, reserved;
  uint64_t instances[GAMEPAD_DEVICE_CAPACITY];
};

struct gamepad_read_request {
  struct message_header header;
  uint64_t flags; /* Zero blocks; POLL returns TIMED_OUT when empty. */
};

struct gamepad_device_request {
  struct message_header header;
  uint64_t instance;
};

/* All operations require INPUT in the object's own space. Acquisition is
 * exclusive and process-owned, independent of display. Copies/closure do not
 * transfer/release it; RELEASE or owner exit does. Empty acquisition succeeds.
 * CATALOG is header-only; INFO/STATE select a boot-unique attachment instance.
 * STATE returns an event-shaped accepted snapshot, never hidden physical state.
 * Catalog generation changes on attach/removal; INFO/STATE for a gone instance
 * return NOT_FOUND. Repeat enumeration if generation changes while collecting.
 *
 * Acquisition queues RESET. Focus/reset discards unread events and invalidates
 * held state. Buttons/hat require release/neutral and a fresh action; axes resume
 * physical values on a fresh focused report. Loss/removal cannot fabricate axis
 * values. Check VALID before consuming state; clear application holds otherwise.
 * RESET has instance zero and requires catalog/state recovery. Continuous state
 * may coalesce only with unchanged discrete state; lost edges cause RESET.
 * One READABLE interest covers every device, focus and topology event. */

_Static_assert(sizeof(struct gamepad_state) == 12, "gamepad state layout");
_Static_assert(sizeof(struct gamepad_event) == 48, "gamepad event layout");
_Static_assert(sizeof(struct gamepad_info) == 64, "gamepad info layout");
_Static_assert(sizeof(struct gamepad_catalog) == 80, "gamepad catalog layout");
_Static_assert(sizeof(struct gamepad_read_request) == 24, "gamepad read layout");
_Static_assert(sizeof(struct gamepad_device_request) == 24, "gamepad device layout");

#endif
