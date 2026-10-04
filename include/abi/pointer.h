#ifndef ABI_POINTER_H
#define ABI_POINTER_H

#include <abi/message.h>

#define POINTER_RIGHT_INPUT (UINT64_C(1) << 0)
#define POINTER_ACQUIRE UINT64_C(1)
#define POINTER_READ UINT64_C(2)
#define POINTER_RELEASE UINT64_C(3)
#define POINTER_READ_POLL (UINT64_C(1) << 0)
#define POINTER_EVENT_FOCUSED (UINT32_C(1) << 0)
#define POINTER_BUTTON_LEFT (UINT32_C(1) << 0)
#define POINTER_BUTTON_RIGHT (UINT32_C(1) << 1)
#define POINTER_BUTTON_MIDDLE (UINT32_C(1) << 2)

enum pointer_event_type {
  POINTER_INPUT,
  POINTER_STATE_RESET,
  POINTER_FOCUS_GAINED,
  POINTER_FOCUS_LOST,
};

/* ACQUIRE/RELEASE are header-only with no reply. INPUT authorizes all calls,
 * only in the object's own space. Acquisition is exclusive, including repeat
 * acquisition by its owner. Copy/close does not transfer/release a session;
 * RELEASE or process exit does. Keyboard and display ownership are independent. */
struct pointer_read_request {
  struct message_header header;
  uint64_t flags; /* Zero blocks; POLL returns TIMED_OUT when empty. */
};

/* READ returns one event. INPUT carries relative device counts without
 * acceleration (+dx right, +dy down, +wheel toward the user) and the buttons
 * held after it; there is no absolute position. flags reports focus at event
 * creation. FOCUS_GAINED/LOST and STATE_RESET carry no motion and no buttons:
 * release all application-held buttons on any of them. Acquisition queues the
 * initial focus event. Focus changes discard stale events. A button held
 * across acquisition, a focus change or a reset is reported only after it is
 * released and pressed again. When the queue is full, motion merges into the
 * newest INPUT event if its buttons are unchanged; otherwise queued events are
 * discarded and STATE_RESET is queued. */
struct pointer_event {
  int32_t dx;
  int32_t dy;
  int32_t wheel;
  uint32_t buttons;
  uint32_t type;
  uint32_t flags;
};

_Static_assert(sizeof(struct pointer_read_request) == 24, "pointer read layout");
_Static_assert(sizeof(struct pointer_event) == 24, "pointer event layout");

#endif
