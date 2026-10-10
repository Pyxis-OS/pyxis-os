#include <abi/key.h>
#include <abi/pointer.h>
#include <abi/syscall.h>
#include <arch/smp.h>
#include <kernel/acpi.h>
#include <kernel/display.h>
#include <kernel/fb/font.h>
#include <kernel/fb/tty.h>
#include <kernel/format.h>
#include <kernel/keyboard.h>
#include <kernel/memory.h>
#include <kernel/object/keyboard.h>
#include <kernel/panic.h>
#include <kernel/pointer.h>
#include <kernel/space.h>
#include <kernel/string.h>
#include <kernel/ui/power_overlay.h>
#include <kernel/volume_ui.h>

/* Layout in font cells. Text rows align with the presenter's bands. */
#define PANEL_COLUMNS 64
#define PANEL_ROWS 12
#define TITLE_ROW 1
#define NOTE_ROW 3
#define STATUS_ROW 4
#define BUTTON_ROW 6
#define BUTTON_ROWS 3
#define HINT_ROW 10
#define BUTTON_COLUMNS 14
#define BUTTON_GAP_COLUMNS 3
#define STATUS_TEXT_BYTES 80

enum choice {
  CHOICE_SHUT_DOWN,
  CHOICE_REBOOT,
  CHOICE_CANCEL,
  CHOICE_COUNT,
};

static const char *const choice_labels[CHOICE_COUNT] = {
  [CHOICE_SHUT_DOWN] = "Shut down",
  [CHOICE_REBOOT] = "Reboot",
  [CHOICE_CANCEL] = "Cancel",
};

enum overlay_state {
  OVERLAY_CLOSED,
  OVERLAY_CHOOSING,
  /* The ACPI worker holds programs and flushes; Cancel no longer applies. */
  OVERLAY_RUNNING,
};

struct overlay_layout {
  size_t x, y, width, height;
  size_t button_x[CHOICE_COUNT];
  size_t button_y, button_width, button_height;
};

static enum overlay_state state;
static struct space *focus_space;
static enum choice selected, hovered;
static enum acpi_power_action running_action;
static char status_text[STATUS_TEXT_BYTES];
static uint64_t visual_generation;

bool power_overlay_shown(void)
{
  return state != OVERLAY_CLOSED;
}

uint64_t power_overlay_generation(void)
{
  return visual_generation;
}

/* Centered in the screen; the panel's top is a whole number of font rows, so
 * every text row lies inside one band. */
static struct overlay_layout layout_for(size_t width, size_t height)
{
  struct overlay_layout layout = {0};
  layout.width = MIN(PANEL_COLUMNS * bizcat.width, width);
  layout.height = PANEL_ROWS * bizcat.height;
  layout.x = (width - layout.width) / 2;
  layout.y = height > layout.height ?
      (height - layout.height) / 2 / bizcat.height * bizcat.height : 0;
  layout.button_width = BUTTON_COLUMNS * bizcat.width;
  layout.button_height = BUTTON_ROWS * bizcat.height;
  layout.button_y = layout.y + BUTTON_ROW * bizcat.height;
  size_t row_width = CHOICE_COUNT * layout.button_width +
      (CHOICE_COUNT - 1) * BUTTON_GAP_COLUMNS * bizcat.width;
  size_t first = layout.x + (layout.width > row_width ? (layout.width - row_width) / 2 : 0);
  for (size_t i = 0; i < CHOICE_COUNT; ++i) {
    layout.button_x[i] = first + i * (layout.button_width + BUTTON_GAP_COLUMNS * bizcat.width);
  }
  return layout;
}

static enum choice button_at(int64_t x, int64_t y)
{
  const struct framebuffer *screen = display_layout();
  struct overlay_layout layout = layout_for(screen->width, screen->height);
  if (x < 0 || y < 0 || (uint64_t)y < layout.button_y ||
      (uint64_t)y - layout.button_y >= layout.button_height) {
    return CHOICE_COUNT;
  }
  for (size_t i = 0; i < CHOICE_COUNT; ++i) {
    if ((uint64_t)x >= layout.button_x[i] &&
        (uint64_t)x - layout.button_x[i] < layout.button_width) {
      return (enum choice)i;
    }
  }
  return CHOICE_COUNT;
}

void power_overlay_open(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  if (state != OVERLAY_CLOSED) {
    return;
  }
  state = OVERLAY_CHOOSING;
  ++visual_generation;
  focus_space = space;
  selected = CHOICE_CANCEL;
  hovered = CHOICE_COUNT;
  status_text[0] = '\0';
  volume_ui_cancel();
  keyboard_set_overlay(space->keyboard, true);
  /* Now unfocused: relative lock is revoked with a fresh-activation
   * requirement, drags end and hovered surfaces see LEAVE. */
  pointer_space_changed(space);
}

static void close_overlay(void)
{
  struct space *space = focus_space;
  state = OVERLAY_CLOSED;
  ++visual_generation;
  focus_space = NULL;
  keyboard_set_overlay(space->keyboard, false);
  pointer_space_changed(space);
}

static void activate(void)
{
  if (selected == CHOICE_CANCEL) {
    close_overlay();
    return;
  }
  enum acpi_power_action action = selected == CHOICE_SHUT_DOWN ?
      ACPI_POWER_OFF : ACPI_POWER_RESTART;
  enum call_status status = acpi_power_local(action);
  if (status == CALL_OK) {
    state = OVERLAY_RUNNING;
    ++visual_generation;
    running_action = action;
    status_text[0] = '\0';
    return;
  }
  const char *message = status == CALL_BUSY ? "Another power operation is running." :
      "Power control is unavailable on this machine.";
  size_t length = strlen(message);
  if (strlen(status_text) != length || memcmp(status_text, message, length)) {
    sprintf(status_text, "%s", message);
    ++visual_generation;
  }
}

void power_overlay_keyboard_input(const struct key_event *event)
{
  KASSERT(arch_cpu_index() == 0);
  if (state != OVERLAY_CHOOSING || event->action == KEY_RELEASE ||
      event->action == KEY_STATE_RESET) {
    return;
  }
  bool press = event->action == KEY_PRESS;
  enum choice previous_selected = selected;
  switch (event->key) {
    case KEY_LEFT:
    case KEY_UP:
      selected = (selected + CHOICE_COUNT - 1) % CHOICE_COUNT;
      break;
    case KEY_RIGHT:
    case KEY_DOWN:
      selected = (selected + 1) % CHOICE_COUNT;
      break;
    case KEY_TAB:
      selected = (event->modifiers & KEY_MOD_SHIFT) ?
          (selected + CHOICE_COUNT - 1) % CHOICE_COUNT : (selected + 1) % CHOICE_COUNT;
      break;
    case KEY_ENTER:
    case KEY_KP_ENTER:
    case KEY_SPACE:
      if (press) {
        activate();
      }
      break;
    case KEY_ESCAPE:
      if (press) {
        close_overlay();
      }
      break;
    default:
      break;
  }
  if (selected != previous_selected) {
    ++visual_generation;
  }
}

void power_overlay_pointer_input(int64_t x, int64_t y, uint32_t pressed)
{
  KASSERT(arch_cpu_index() == 0);
  if (state != OVERLAY_CHOOSING) {
    hovered = CHOICE_COUNT;
    return;
  }
  enum choice next_hovered = button_at(x, y);
  if (hovered != next_hovered) {
    hovered = next_hovered;
    ++visual_generation;
  }
  if (hovered != CHOICE_COUNT && (pressed & POINTER_BUTTON_LEFT)) {
    if (selected != hovered) {
      ++visual_generation;
    }
    selected = hovered;
    activate();
  }
}

void power_overlay_update(void)
{
  KASSERT(arch_cpu_index() == 0);
  enum call_status status;
  if (state != OVERLAY_RUNNING || !acpi_power_local_failed(&status)) {
    return;
  }
  state = OVERLAY_CHOOSING;
  ++visual_generation;
  selected = CHOICE_CANCEL;
  sprintf(status_text, "%s failed (status %d); the system stays up.",
      running_action == ACPI_POWER_OFF ? "Shutdown" : "Restart", (int)status);
}

/* Fill the part of a screen rectangle that lies inside the band at TOP. */
static void fill_clipped(struct framebuffer *band, size_t top, size_t x, size_t y,
    size_t width, size_t height, uint32_t color)
{
  size_t bottom = top + band->height;
  if (y >= bottom || y + height <= top || x >= band->width) {
    return;
  }
  size_t first = MAX(y, top);
  size_t last = MIN(y + height, bottom);
  fb_fill_rect(band, x, first - top, MIN(width, band->width - x), last - first, color);
}

static void outline_clipped(struct framebuffer *band, size_t top, size_t x, size_t y,
    size_t width, size_t height, uint32_t color)
{
  fill_clipped(band, top, x, y, width, 1, color);
  fill_clipped(band, top, x, y + height - 1, width, 1, color);
  fill_clipped(band, top, x, y, 1, height, color);
  fill_clipped(band, top, x + width - 1, y, 1, height, color);
}

/* Text centered on [X, X + WIDTH) at screen row Y, drawn only in its band.
 * Characters that do not fit are left out. */
static void text_clipped(struct framebuffer *band, size_t top, size_t x, size_t width,
    size_t y, const char *text, uint32_t foreground, uint32_t background)
{
  if (y != top) {
    return;
  }
  size_t length = MIN(strlen(text), width / bizcat.width);
  size_t left = x + (width - length * bizcat.width) / 2;
  for (size_t i = 0; i < length; ++i) {
    size_t column = left + i * bizcat.width;
    if (column + bizcat.width <= band->width) {
      tty_plot_char_raw(band, &bizcat, text[i], column, 0, foreground, background);
    }
  }
}

void power_overlay_draw_band(struct framebuffer *band, size_t top,
    size_t width, size_t height)
{
  const struct color_scheme *scheme = &aardvark_scheme;
  uint32_t background = scheme->palette[0];
  uint32_t dim = scheme->palette[8];
  struct overlay_layout layout = layout_for(width, height);
  fb_fill_rect(band, 0, 0, band->width, band->height, background);
  outline_clipped(band, top, layout.x, layout.y, layout.width, layout.height, dim);

  size_t row = bizcat.height;
  text_clipped(band, top, layout.x, layout.width, layout.y + TITLE_ROW * row,
      "Shut down or restart", scheme->foreground, background);
  text_clipped(band, top, layout.x, layout.width, layout.y + NOTE_ROW * row,
      "Programs are stopped and storage pools are flushed first.",
      scheme->foreground, background);

  bool running = state == OVERLAY_RUNNING;
  const char *status = status_text;
  if (running) {
    status = running_action == ACPI_POWER_OFF ? "Shutting down: flushing pools..." :
        "Restarting: flushing pools...";
  }
  text_clipped(band, top, layout.x, layout.width, layout.y + STATUS_ROW * row, status,
      running ? scheme->foreground : scheme->palette[9], background);

  size_t label_y = layout.button_y + (BUTTON_ROWS / 2) * row;
  for (size_t i = 0; i < CHOICE_COUNT; ++i) {
    bool chosen = !running && i == selected;
    uint32_t fill = chosen ? scheme->foreground : background;
    uint32_t text = running ? dim : chosen ? background : scheme->foreground;
    uint32_t edge = !running && i == hovered ? scheme->foreground : dim;
    fill_clipped(band, top, layout.button_x[i], layout.button_y, layout.button_width,
        layout.button_height, fill);
    outline_clipped(band, top, layout.button_x[i], layout.button_y, layout.button_width,
        layout.button_height, edge);
    text_clipped(band, top, layout.button_x[i], layout.button_width, label_y,
        choice_labels[i], text, fill);
  }
  text_clipped(band, top, layout.x, layout.width, layout.y + HINT_ROW * row,
      running ? "Please wait." : "Arrows or Tab: choose   Enter: confirm   Esc: cancel",
      scheme->palette[7], background);
}
