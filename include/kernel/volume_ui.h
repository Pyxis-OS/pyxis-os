#ifndef KERNEL_VOLUME_UI_H
#define KERNEL_VOLUME_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct space;
struct framebuffer;
struct pointer_frame;
struct key_event;

#define VOLUME_ICON_WIDTH 24

/* A frame records whole bar slots. Spaces are retained for the boot. */
struct volume_ui_layout {
  struct space *first_space;
  size_t tab_x, tab_width, tab_count;
  size_t master_x, screen_width, screen_height;
  bool master_shown, spaces_shown;
};

/* Sole BSP presenter: prepare/draw one immutable frame, then publish its
 * geometry only after confirmed presentation. No program backing is changed. */
void volume_ui_begin_frame(const struct volume_ui_layout *layout,
    const struct framebuffer *navigation);
void volume_ui_draw_icon(struct framebuffer *navigation, struct space *space,
    size_t x, bool enabled);
void volume_ui_present_copy(const struct framebuffer *layout,
    const struct pointer_frame *pointer, size_t offset, const void *pixels, size_t bytes);
void volume_ui_end_frame(bool presented);

/* BSP, IF=0, once per service tick before deciding whether to compose. */
void volume_ui_update(void);
uint64_t volume_ui_generation(void);

/* BSP, IF=0. Consumed physical buttons remain suppressed by pointer routing. */
bool volume_ui_pointer_input(int64_t x, int64_t y, int32_t wheel,
    uint32_t buttons, uint32_t pressed, bool content_drag);
bool volume_ui_pointer_over(int64_t x, int64_t y);
bool volume_ui_keyboard_input(const struct key_event *event);
bool volume_ui_keyboard_focused(void);
void volume_ui_cancel(void);

#endif
