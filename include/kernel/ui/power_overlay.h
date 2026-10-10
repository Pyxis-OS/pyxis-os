#ifndef KERNEL_UI_POWER_OVERLAY_H
#define KERNEL_UI_POWER_OVERLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct framebuffer;
struct key_event;
struct space;

/* The Ctrl+Alt+Delete emergency screen: Shut down, Reboot and Cancel over
 * the whole display. The kernel draws it and asks the ACPI worker for power
 * with the power button's physical-access authority. While it is shown, the
 * focused space's keyboard is in overlay focus and its pointer surfaces are
 * unfocused; capture is kept and relative lock is revoked.
 *
 * Everything here is BSP, IF=0, from the presenter task. */

bool power_overlay_shown(void);

/* Show the overlay over SPACE, the active space. Does nothing while shown. */
void power_overlay_open(struct space *space);

/* Every key event while shown; events are consumed by the caller. */
void power_overlay_keyboard_input(const struct key_event *event);

/* Every pointer report while shown, at the clamped physical position.
 * Consumed buttons remain suppressed by pointer routing. */
void power_overlay_pointer_input(int64_t x, int64_t y, uint32_t pressed);

/* Once per overlay frame, before drawing: collects a failed power operation. */
void power_overlay_update(void);

/* Draw the overlay's rows [TOP, TOP + BAND->height) of a WIDTH x HEIGHT
 * screen into BAND. TOP is a multiple of the font height. */
void power_overlay_draw_band(struct framebuffer *band, size_t top,
    size_t width, size_t height);

#endif
