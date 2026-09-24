#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>

#include <abi/key.h>

struct key_event {
  enum key_code key;
  enum key_action action;
  unsigned modifiers; /* State after this event, including lock toggles. */
};

bool keyboard_available(void);

/* One consumer on the BSP, outside interrupt entry. Nonblocking; false means
 * no complete event is available and leaves *event unchanged. Preserves IF.
 * Decoding happens here, not in the IRQ. Call regularly to drain the queue.
 * On lost input, KEY_STATE_RESET with KEY_NONE clears held keys and modifiers;
 * the consumer must discard its held-key state too. Lock LEDs are not updated. */
bool keyboard_read_event(struct key_event *event);

/* Session-side US ASCII/terminal mapping, independent of the device decoder.
 * Call after consuming global shortcuts. Returns 0 for keys without a binding;
 * otherwise writes one complete sequence, without a string terminator. */
#define KEY_TEXT_MAX 4
size_t keyboard_text(const struct key_event *event, char bytes[KEY_TEXT_MAX]);

#endif
