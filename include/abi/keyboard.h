#ifndef ABI_KEYBOARD_H
#define ABI_KEYBOARD_H

#include <abi/key.h>
#include <abi/message.h>

#define KEYBOARD_RIGHT_INPUT (UINT64_C(1) << 0)
#define KEYBOARD_ACQUIRE UINT64_C(1)
#define KEYBOARD_READ UINT64_C(2)
#define KEYBOARD_RELEASE UINT64_C(3)
#define KEYBOARD_READ_POLL (UINT64_C(1) << 0)
#define KEYBOARD_EVENT_FOCUSED (UINT32_C(1) << 0)

/* ACQUIRE/RELEASE are header-only with no reply. INPUT authorizes all calls,
 * only in the object's own space. Acquisition is exclusive, including repeat
 * acquisition by its owner. Copy/close does not transfer/release a session;
 * RELEASE or process exit does. Display ownership is independent. */
struct keyboard_read_request {
  struct message_header header;
  uint64_t flags; /* Zero blocks; POLL returns TIMED_OUT when empty. */
};

/* READ returns one event. Key/action/modifiers use abi/key.h; flags reports
 * current focus at event creation. FOCUS_GAINED/LOST and STATE_RESET use
 * KEY_NONE and zero modifiers: clear all application-held keys on any of them.
 * Acquisition queues the initial focus event. Focus changes discard stale
 * events, so rapid switches may coalesce to the most recent focus state.
 * Queue/driver loss discards unreliable events and queues STATE_RESET.
 * Held keys across acquisition/focus changes require release and a fresh press.
 * Captured input does not also enter the terminal's text queue. A hidden graphics
 * layer retains capture but loses focus; text goes to the console instead.
 * Super+Left/Right and Super+Up/Down remain kernel navigation; their arrow
 * press/repeat/release is consumed. */
struct keyboard_event {
  uint32_t key;
  uint32_t action;
  uint32_t modifiers;
  uint32_t flags;
};

_Static_assert(sizeof(struct keyboard_read_request) == 24, "keyboard read layout");
_Static_assert(sizeof(struct keyboard_event) == 16, "keyboard event layout");

#endif
