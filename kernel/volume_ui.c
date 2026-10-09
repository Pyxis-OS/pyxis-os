#include <abi/pointer.h>
#include <arch/cpu.h>
#include <kernel/audio.h>
#include <kernel/fb/font.h>
#include <kernel/fb/tty.h>
#include <kernel/keyboard.h>
#include <kernel/memory.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/clipboard.h>
#include <kernel/pointer.h>
#include <kernel/pointer_present.h>
#include <kernel/space.h>
#include <kernel/string.h>
#include <kernel/volume_icons.h>
#include <kernel/volume_ui.h>

#define BAR_HEIGHT 32
#define POPUP_WIDTH 112
#define POPUP_HEIGHT 192
#define POPUP_MIN_HEIGHT 96
#define TRACK_TOP 64
#define TRACK_BOTTOM_MARGIN 12
#define TRACK_WIDTH 24

struct popup_layout {
  struct space *space; /* NULL is master. */
  size_t x, y, width, height;
  bool shown;
};

static struct volume_ui_layout frame_layout, drawn_layout;
static struct popup_layout frame_popup, drawn_popup;
static bool drawn_valid, open, focused, dragging;
static uint64_t cancel_generation, frame_cancel_generation;
static struct space *target, *focus_space;
static uint32_t popup_pixels[POPUP_WIDTH * POPUP_HEIGHT];

static bool contains(int64_t x, int64_t y, size_t left, size_t top,
    size_t width, size_t height)
{
  return x >= 0 && y >= 0 && (uint64_t)x >= left && (uint64_t)y >= top &&
      (uint64_t)x - left < width && (uint64_t)y - top < height;
}

static bool target_slot(const struct volume_ui_layout *layout,
    struct space *space, size_t *x)
{
  if (!space) {
    *x = layout->master_x;
    return layout->master_shown;
  }
  if (!layout->spaces_shown) {
    return false;
  }
  struct space *tab = layout->first_space;
  for (size_t i = 0; i < layout->tab_count; ++i, tab = tab->next) {
    if (tab == space) {
      *x = layout->tab_x + i * layout->tab_width + layout->tab_width - VOLUME_ICON_WIDTH;
      return true;
    }
  }
  return false;
}

static bool icon_hit(const struct volume_ui_layout *layout,
    int64_t x, int64_t y, struct space **space)
{
  if (layout->master_shown && contains(x, y, layout->master_x, 0,
      VOLUME_ICON_WIDTH, BAR_HEIGHT)) {
    *space = NULL;
    return true;
  }
  if (!layout->spaces_shown || !layout->tab_width || x < 0 || y < 0 ||
      y >= BAR_HEIGHT || (uint64_t)x < layout->tab_x) {
    return false;
  }
  size_t index = ((uint64_t)x - layout->tab_x) / layout->tab_width;
  if (index >= layout->tab_count ||
      ((uint64_t)x - layout->tab_x) % layout->tab_width <
        layout->tab_width - VOLUME_ICON_WIDTH) {
    return false;
  }
  *space = layout->first_space;
  for (size_t i = 0; i < index; ++i) {
    *space = (*space)->next;
  }
  return true;
}

static bool enabled(struct space *space)
{
  struct audio_volume_snapshot snapshot;
  return space != space_caelum() && audio_volume_snapshot(space, &snapshot);
}

static bool popup_fits(const struct volume_ui_layout *layout)
{
  return layout->screen_width >= POPUP_WIDTH &&
      layout->screen_height >= BAR_HEIGHT + POPUP_MIN_HEIGHT;
}

static void set_focus(bool next)
{
  if (focused == next) {
    return;
  }
  if (next) {
    focus_space = space_pointer_active();
    clipboard_space_cancel(focus_space);
    keyboard_set_overlay(focus_space->keyboard, true);
  } else {
    keyboard_set_overlay(focus_space->keyboard, false);
    focus_space = NULL;
  }
  focused = next;
}

void volume_ui_cancel(void)
{
  ++cancel_generation;
  set_focus(false);
  open = dragging = false;
  drawn_popup.shown = false;
}

bool volume_ui_keyboard_focused(void)
{
  return focused;
}

static bool show_popup(struct space *space, bool keyboard_focus)
{
  size_t x;
  if (!drawn_valid || !popup_fits(&drawn_layout) ||
      !target_slot(&drawn_layout, space, &x) || !enabled(space)) {
    return false;
  }
  target = space;
  open = true;
  if (keyboard_focus) {
    set_focus(true);
  }
  return true;
}

static bool in_popup(int64_t x, int64_t y)
{
  return drawn_popup.shown && contains(x, y, drawn_popup.x, drawn_popup.y,
      drawn_popup.width, drawn_popup.height);
}

bool volume_ui_pointer_over(int64_t x, int64_t y)
{
  struct space *space;
  return drawn_valid && (icon_hit(&drawn_layout, x, y, &space) || in_popup(x, y));
}

static void track_level(const struct popup_layout *popup, int64_t y)
{
  int64_t top = popup->y + TRACK_TOP;
  int64_t bottom = popup->y + popup->height - TRACK_BOTTOM_MARGIN;
  int value = y <= top ? 100 : y >= bottom ? 0 :
      (int)((bottom - y) * 100 / (bottom - top));
  audio_volume_control(target, AUDIO_VOLUME_SET_PERCENT, value);
}

bool volume_ui_pointer_input(int64_t x, int64_t y, int32_t wheel,
    uint32_t buttons, uint32_t pressed, bool content_drag)
{
  if (!drawn_valid || content_drag) {
    return false;
  }
  if (dragging) {
    if (buttons & POINTER_BUTTON_LEFT) {
      track_level(&drawn_popup, y);
    } else {
      dragging = false;
    }
    return true;
  }
  struct space *space;
  bool icon = icon_hit(&drawn_layout, x, y, &space);
  bool popup = in_popup(x, y);
  if (icon) {
    if ((!focused || pressed) && !show_popup(space, false)) {
      volume_ui_cancel();
    }
    if (enabled(space) && (pressed & POINTER_BUTTON_LEFT)) {
      audio_volume_control(space, AUDIO_VOLUME_TOGGLE_MUTE, 0);
    }
    if (enabled(space) && wheel) {
      audio_volume_control(space, AUDIO_VOLUME_ADJUST_PERCENT,
          wheel > 20 ? 100 : wheel < -20 ? -100 : wheel * 5);
    }
    return true;
  }
  if (popup) {
    target = drawn_popup.space;
    open = true;
    if (pressed & POINTER_BUTTON_LEFT) {
      set_focus(true);
      if (contains(x, y, drawn_popup.x + (POPUP_WIDTH - TRACK_WIDTH) / 2,
          drawn_popup.y + TRACK_TOP, TRACK_WIDTH,
          drawn_popup.height - TRACK_TOP - TRACK_BOTTOM_MARGIN + 1)) {
        dragging = true;
        track_level(&drawn_popup, y);
      }
    }
    if (wheel) {
      audio_volume_control(target, AUDIO_VOLUME_ADJUST_PERCENT,
          wheel > 20 ? 100 : wheel < -20 ? -100 : wheel * 5);
    }
    return true;
  }
  if (open && pressed) {
    volume_ui_cancel();
    return true;
  }
  if (open && !focused) {
    volume_ui_cancel();
  }
  return false;
}

bool volume_ui_keyboard_input(const struct key_event *event)
{
  unsigned modifiers = event->modifiers &
      (KEY_MOD_SHIFT | KEY_MOD_CONTROL | KEY_MOD_ALT | KEY_MOD_SUPER);
  if (event->key == KEY_M && modifiers == KEY_MOD_SUPER) {
    if (event->action == KEY_PRESS) {
      audio_volume_control(NULL, AUDIO_VOLUME_TOGGLE_MUTE, 0);
    }
    return true;
  }
  if (event->key == KEY_G && !pointer_locked() &&
      (modifiers == KEY_MOD_SUPER || modifiers == (KEY_MOD_SUPER | KEY_MOD_SHIFT))) {
    if (event->action == KEY_PRESS) {
      show_popup(modifiers & KEY_MOD_SHIFT ? space_pointer_active() : NULL, true);
    }
    return true;
  }
  if (!focused) {
    return false;
  }
  if (event->action == KEY_RELEASE) {
    return true;
  }
  switch (event->key) {
    case KEY_ESCAPE:
      volume_ui_cancel();
      break;
    case KEY_UP:
    case KEY_RIGHT:
      audio_volume_control(target, AUDIO_VOLUME_ADJUST_PERCENT, 1);
      break;
    case KEY_DOWN:
    case KEY_LEFT:
      audio_volume_control(target, AUDIO_VOLUME_ADJUST_PERCENT, -1);
      break;
    case KEY_PAGE_UP:
      audio_volume_control(target, AUDIO_VOLUME_ADJUST_PERCENT, 5);
      break;
    case KEY_PAGE_DOWN:
      audio_volume_control(target, AUDIO_VOLUME_ADJUST_PERCENT, -5);
      break;
    case KEY_HOME:
      audio_volume_control(target, AUDIO_VOLUME_SET_PERCENT, 0);
      break;
    case KEY_END:
      audio_volume_control(target, AUDIO_VOLUME_SET_PERCENT, 100);
      break;
    case KEY_SPACE:
      if (event->action == KEY_PRESS) {
        audio_volume_control(target, AUDIO_VOLUME_TOGGLE_MUTE, 0);
      }
      break;
    default:
      break;
  }
  return true;
}

void volume_ui_draw_icon(struct framebuffer *navigation, struct space *space,
    size_t x, bool control_enabled)
{
  struct audio_volume_snapshot snapshot;
  bool available = control_enabled && audio_volume_snapshot(space, &snapshot);
  unsigned percent = available ? snapshot.space_percent : 0;
  bool muted = !available || snapshot.space_muted;
  unsigned icon = muted || !percent ? 3 : percent < 34 ? 2 : percent < 67 ? 1 : 0;
  uint32_t color = framebuffer_color(navigation,
      available ? aardvark_scheme.foreground : aardvark_scheme.palette[8]);
  for (size_t y = 0; y < VOLUME_ICON_WIDTH; ++y) {
    uint32_t *row = (uint32_t *)(navigation->address +
        ((BAR_HEIGHT - VOLUME_ICON_WIDTH) / 2 + y) * navigation->pitch) + x;
    for (size_t column = 0; column < VOLUME_ICON_WIDTH; ++column) {
      if (volume_icon_masks[icon][y] & (UINT32_C(1) << column)) {
        row[column] = color;
      }
    }
  }
}

static void popup_text(struct framebuffer *fb, const char *text, size_t y)
{
  size_t length = strlen(text);
  size_t x = (fb->width - length * bizcat.width) / 2;
  for (size_t i = 0; i < length; ++i) {
    tty_plot_char_raw(fb, &bizcat, text[i], x + i * bizcat.width, y,
        aardvark_scheme.foreground, aardvark_scheme.palette[0]);
  }
}

void volume_ui_begin_frame(const struct volume_ui_layout *layout,
    const struct framebuffer *navigation)
{
  uint64_t flags = cpu_save_interrupts();
  frame_layout = *layout;
  frame_popup = (struct popup_layout){0};
  size_t slot_x;
  struct audio_volume_snapshot snapshot;
  bool visible = open && popup_fits(layout) &&
      target_slot(layout, target, &slot_x) && enabled(target) &&
      audio_volume_snapshot(target, &snapshot);
  if (open && !visible) {
    volume_ui_cancel();
  }
  if (visible) {
    size_t center = slot_x + VOLUME_ICON_WIDTH / 2;
    size_t x = center > POPUP_WIDTH / 2 ? center - POPUP_WIDTH / 2 : 0;
    x = MIN(x, layout->screen_width - POPUP_WIDTH);
    frame_popup = (struct popup_layout){.space = target, .x = x, .y = BAR_HEIGHT,
      .width = POPUP_WIDTH, .height = MIN(POPUP_HEIGHT, layout->screen_height - BAR_HEIGHT),
      .shown = true};
  }
  frame_cancel_generation = cancel_generation;
  cpu_restore_interrupts(flags);
  if (!visible) {
    return;
  }
  struct framebuffer popup = *navigation;
  popup.address = (uintptr_t)popup_pixels;
  popup.width = POPUP_WIDTH;
  popup.height = frame_popup.height;
  popup.pitch = POPUP_WIDTH * sizeof(uint32_t);
  popup.size = popup.height * popup.pitch;
  fb_fill_rect(&popup, 0, 0, popup.width, popup.height, aardvark_scheme.palette[0]);
  fb_rect(&popup, 0, 0, popup.width, popup.height, aardvark_scheme.palette[8]);
  unsigned percent = snapshot.space_percent;
  char text[] = {(char)('0' + percent / 100), (char)('0' + percent / 10 % 10),
    (char)('0' + percent % 10), '%', '\0'};
  popup_text(&popup, text, 4);
  popup_text(&popup, snapshot.space_muted ? "Muted" : "Unmuted", 22);
  if (frame_popup.space) {
    popup_text(&popup, snapshot.master_muted ? "Master muted" : "Master on", 40);
  }
  size_t bottom = popup.height - TRACK_BOTTOM_MARGIN;
  size_t knob_y = bottom - percent * (bottom - TRACK_TOP) / 100;
  fb_fill_rect(&popup, POPUP_WIDTH / 2 - 1, TRACK_TOP, 2,
      bottom - TRACK_TOP + 1, aardvark_scheme.palette[8]);
  fb_fill_rect(&popup, POPUP_WIDTH / 2 - 1, knob_y, 2,
      bottom - knob_y + 1, aardvark_scheme.foreground);
  fb_fill_rect(&popup, (POPUP_WIDTH - TRACK_WIDTH) / 2, knob_y - 2,
      TRACK_WIDTH, 5, aardvark_scheme.foreground);
}

void volume_ui_end_frame(bool presented)
{
  drawn_valid = presented;
  if (!presented) {
    uint64_t flags = cpu_save_interrupts();
    volume_ui_cancel();
    cpu_restore_interrupts(flags);
  }
  if (presented) {
    drawn_layout = frame_layout;
    drawn_popup = frame_popup;
    /* A lock/focus boundary can cancel while this immutable frame composes.
     * Its pixels may finish, but its former popup cannot regain input. */
    if (frame_cancel_generation != cancel_generation) {
      drawn_popup.shown = false;
    }
  }
}

void volume_ui_present_copy(const struct framebuffer *layout,
    const struct pointer_frame *pointer, size_t offset, const void *pixels, size_t bytes)
{
  if (!frame_popup.shown) {
    pointer_present_copy(layout, pointer, offset, pixels, bytes);
    return;
  }
  const uint8_t *source = pixels;
  size_t end = offset + bytes;
  for (size_t y = 0; y < frame_popup.height; ++y) {
    size_t start = (frame_popup.y + y) * layout->pitch + frame_popup.x * sizeof(uint32_t);
    size_t stop = start + frame_popup.width * sizeof(uint32_t);
    if (stop <= offset) {
      continue;
    }
    if (start >= end) {
      break;
    }
    if (offset < start) {
      size_t count = start - offset;
      pointer_present_copy(layout, pointer, offset, source, count);
      offset += count;
      source += count;
    }
    size_t count = MIN(stop, end) - offset;
    pointer_present_copy(layout, pointer, offset,
        (const uint8_t *)(popup_pixels + y * POPUP_WIDTH) + offset - start, count);
    offset += count;
    source += count;
  }
  pointer_present_copy(layout, pointer, offset, source, end - offset);
}
