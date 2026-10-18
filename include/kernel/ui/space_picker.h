#ifndef KERNEL_UI_SPACE_PICKER_H
#define KERNEL_UI_SPACE_PICKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct framebuffer;
struct key_event;
struct space;

/* BSP presenter ownership. State-changing calls require IF=0. The registry
 * retains spaces; the picker borrows them and allocates no storage. */
bool space_picker_shown(void);
uint64_t space_picker_generation(void);
void space_picker_open(struct space *space);
void space_picker_cancel(void);

/* Enter returns the selected space while keeping overlay focus. Switch the
 * active space before cancelling, so the old space is not briefly refocused. */
struct space *space_picker_keyboard_input(const struct key_event *event);
void space_picker_draw_band(struct framebuffer *band, size_t top,
    size_t width, size_t height);

#endif
