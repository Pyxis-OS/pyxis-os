#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include <stdint.h>

#define MOUSE_BUTTON_LEFT (1u << 0)
#define MOUSE_BUTTON_RIGHT (1u << 1)
#define MOUSE_BUTTON_MIDDLE (1u << 2)

/* Relative motion in device counts, without acceleration. Signs follow the
 * display: +dx is right, +dy is down and +wheel scrolls toward the user. */
struct mouse_event {
  int32_t dx, dy, wheel;
  unsigned buttons; /* Buttons held after this event. */
  bool reset;       /* Input was lost; other fields are zero and no button is held. */
};

bool mouse_available(void);

/* One consumer on the BSP, outside interrupt entry. Nonblocking; false means
 * no complete packet is available and leaves *event unchanged. Preserves IF.
 * Decoding happens here, not in the IRQ. Call regularly to drain the queue.
 * After a reset event the consumer must release any buttons it considers held. */
bool mouse_read_event(struct mouse_event *event);

#endif
