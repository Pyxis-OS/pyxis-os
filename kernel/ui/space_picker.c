#include <abi/key.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/defs.h>
#include <kernel/fb/font.h>
#include <kernel/fb/tty.h>
#include <kernel/keyboard.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/gamepad.h>
#include <kernel/panic.h>
#include <kernel/pointer.h>
#include <kernel/space.h>
#include <kernel/string.h>
#include <kernel/ui/space_picker.h>
#include <kernel/ui/power_overlay.h>
#include <kernel/volume_ui.h>

#define PICKER_COLUMNS 64
#define PICKER_ROWS 16

static struct space *focus_space, *selected;
static size_t viewport_first;
static uint64_t visual_generation;

bool space_picker_shown(void)
{
  return focus_space != NULL;
}

uint64_t space_picker_generation(void)
{
  return visual_generation;
}

void space_picker_open(struct space *space)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  if (focus_space) {
    return;
  }
  focus_space = selected = space;
  viewport_first = 0;
  ++visual_generation;
  volume_ui_cancel();
  keyboard_set_overlay(space->keyboard, true);
  gamepad_set_overlay(space->gamepad, true);
  pointer_space_changed(space);
}

void space_picker_cancel(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  if (!focus_space) {
    return;
  }
  struct space *space = focus_space;
  focus_space = selected = NULL;
  ++visual_generation;
  /* Power takeover retains modal focus throughout the handoff. */
  if (!power_overlay_shown()) {
    keyboard_set_overlay(space->keyboard, false);
    gamepad_set_overlay(space->gamepad, false);
  }
  pointer_space_changed(space);
}

struct space *space_picker_keyboard_input(const struct key_event *event)
{
  if (!focus_space || event->action == KEY_RELEASE) {
    return NULL;
  }
  if (event->key == KEY_ENTER && event->action == KEY_PRESS) {
    return selected;
  }
  if (event->key == KEY_ESCAPE && event->action == KEY_PRESS) {
    space_picker_cancel();
    return NULL;
  }
  struct space *next = selected;
  if (event->key == KEY_DOWN) {
    if (selected->next) {
      next = selected->next;
    }
  } else if (event->key == KEY_UP) {
    for (struct space *space = space_caelum(); space && space != selected; space = space->next) {
      next = space;
    }
  } else if (event->key == KEY_HOME) {
    next = space_caelum();
  } else if (event->key == KEY_END) {
    while (next->next) {
      next = next->next;
    }
  }
  if (next != selected) {
    selected = next;
    ++visual_generation;
  }
  return NULL;
}

static void draw_text(struct framebuffer *band, size_t x, size_t width,
    const char *text, uint32_t foreground, uint32_t background)
{
  size_t length = MIN(strlen(text), width / bizcat.width);
  for (size_t i = 0; i < length; ++i) {
    tty_plot_char_raw(band, &bizcat, (unsigned char)text[i], x + i * bizcat.width,
        0, foreground, background);
  }
}

void space_picker_draw_band(struct framebuffer *band, size_t top,
    size_t width, size_t height)
{
  const struct color_scheme *scheme = &aardvark_scheme;
  fb_fill_rect(band, 0, 0, band->width, band->height, scheme->background);
  size_t available_rows = height / bizcat.height;
  if (!focus_space || available_rows < 3) {
    return;
  }
  size_t count = 0, selected_index = 0;
  for (struct space *space = space_caelum(); space; space = space->next) {
    if (space == selected) {
      selected_index = count;
    }
    ++count;
  }
  size_t rows = MIN(count, MIN(PICKER_ROWS - 2, available_rows - 2));
  if (selected_index < viewport_first) {
    viewport_first = selected_index;
  } else if (selected_index - viewport_first >= rows) {
    viewport_first = selected_index - rows + 1;
  }
  size_t panel_height = (rows + 2) * bizcat.height;
  size_t y = (height - panel_height) / 2 / bizcat.height * bizcat.height;
  if (top < y || top >= y + panel_height) {
    return;
  }
  size_t panel_width = MIN(PICKER_COLUMNS * bizcat.width, width);
  size_t x = (width - panel_width) / 2;
  size_t row = (top - y) / bizcat.height;
  uint32_t foreground = scheme->foreground, background = scheme->palette[0];
  struct space *space = NULL;
  if (row && row <= rows) {
    space = space_caelum();
    for (size_t i = 0; space && i < viewport_first + row - 1; ++i) {
      space = space->next;
    }
    if (space == selected) {
      foreground = scheme->palette[0];
      background = scheme->foreground;
    }
  }
  fb_fill_rect(band, x, 0, panel_width, band->height, background);
  size_t padding = MIN(bizcat.width, panel_width);
  size_t text_width = panel_width - padding;
  if (!row) {
    draw_text(band, x + padding, text_width, "Spaces", foreground, background);
  } else if (row == rows + 1) {
    draw_text(band, x + padding, text_width, "Up/Down  Enter: select  Esc: cancel",
        scheme->palette[8], background);
  } else if (space) {
    char title[SPACE_TITLE_MAX + 1];
    space_title_snapshot(space, title);
    draw_text(band, x + padding, text_width, title, foreground, background);
  }
}
