//
// Created by chronium on 9/13/26.
//

#include <kernel/fb/tty.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>

const struct color_scheme aardvark_scheme = {
    .palette = {0x222734, 0xc26265, 0x52aa60, 0xad9b49, 0x487fd4, 0xaf5bd1,
                0x269d9a, 0x5a6377, 0x3a4152, 0xe48383, 0x75cf84, 0xc7b461,
                0x76a8f2, 0xd58bf0, 0x52c4c0, 0xdfe5ee},
    .foreground = 0xb4bcca,
    .background = 0x0f141f,
    .cursor = 0xb4bcca,
    .cursor_text = 0x0f141f,
    .selection = 0x0f141f,
    .selection_background = 0xb4bcca
};

struct tty global_tty = {.geometry_generation = 1};

static uint8_t glyph_index(const struct font *font, char c)
{
  uint8_t index = (unsigned char)c;
  return index <= font->max_glyph ? index : '?';
}

void tty_plot_char_raw(const struct framebuffer *fb, const struct font *font,
    char c, size_t x, size_t y, uint32_t fg, uint32_t bg)
{
  const uint8_t *glyph =
      font->data + (size_t)glyph_index(font, c) * font->stride;
  uint32_t foreground = framebuffer_color(fb, fg);
  uint32_t background = framebuffer_color(fb, bg);

  for (size_t row = 0; row < font->height; ++row) {
    volatile uint32_t *pixel_row =
        (volatile uint32_t *)((uint8_t *)fb->address +
                              (y + row) * fb->pitch);

    for (size_t col = 0; col < font->width; ++col) {
      bool bit =
          (glyph[row] >>
           (col % 8)) & 1;

      pixel_row[x + col] = bit ? foreground : background;
    }
  }

  cpu_store_fence();
}

void tty_plot_char(struct tty *tty, char c, uint16_t x, uint16_t y,
               uint32_t fg, uint32_t bg)
{
  const struct font *font = tty->font;
  uint8_t glyph = glyph_index(font, c);
  if (tty->cells) {
    KASSERT(x < tty->width && y < tty->height);
    size_t index = (size_t)y * tty->width + x;
    if ((tty->selection_valid || tty->selection_dragging) && tty->cells[index] != glyph) {
      size_t first = MIN(tty->selection_anchor, tty->selection_endpoint);
      size_t last = MAX(tty->selection_anchor, tty->selection_endpoint);
      if (index >= first && index <= last) {
        tty_selection_clear(tty);
      }
    }
    tty->cells[index] = glyph;
  }

  size_t x_dst = (size_t)x * font->width;
  size_t y_dst = (size_t)y * font->height;

  tty_plot_char_raw(tty->fb, font, (char)glyph, x_dst, y_dst, fg, bg);
}

static void tty_newline(struct tty *tty)
{
  tty->wrap_pending = false;
  tty->x = 0;
  tty->y++;

  if (tty->y >= tty->height) {
    size_t row_bytes = tty->fb->pitch * tty->font->height;
    void *pixels = (void *)tty->fb->address;

    tty_selection_clear(tty);
    if (tty->cells) {
      memmove(tty->cells, tty->cells + tty->width,
          (size_t)tty->width * (tty->height - 1));
    }
    memmove(pixels, (uint8_t *)pixels + row_bytes,
            row_bytes * (tty->height - 1));
    tty->y = tty->height - 1;

    for (size_t x = 0; x < tty->width; ++x) {
      tty_plot_char(tty, ' ', x, tty->y, tty->fg, tty->bg);
    }
  }
}

static void tty_draw_cell(struct tty *tty, char c, size_t x, size_t y)
{
  uint32_t fg = tty->reverse ? tty->bg : tty->fg;
  uint32_t bg = tty->reverse ? tty->fg : tty->bg;
  tty_plot_char(tty, c, x, y, fg, bg);
}

static void erase_cells(struct tty *tty, size_t first, size_t end)
{
  for (size_t cell = first; cell < end; ++cell) {
    tty_draw_cell(tty, ' ', cell % tty->width, cell / tty->width);
  }
}

static void select_style(struct tty *tty, unsigned parameter)
{
  if (parameter == 0) {
    tty->fg = tty->scheme->foreground;
    tty->bg = tty->scheme->background;
    tty->reverse = false;
  } else if (parameter == 7 || parameter == 27) {
    tty->reverse = parameter == 7;
  } else if (parameter == 39) {
    tty->fg = tty->scheme->foreground;
  } else if (parameter == 49) {
    tty->bg = tty->scheme->background;
  } else if (parameter >= 30 && parameter <= 37) {
    tty->fg = tty->scheme->palette[parameter - 30];
  } else if (parameter >= 40 && parameter <= 47) {
    tty->bg = tty->scheme->palette[parameter - 40];
  } else if (parameter >= 90 && parameter <= 97) {
    tty->fg = tty->scheme->palette[parameter - 90 + 8];
  } else if (parameter >= 100 && parameter <= 107) {
    tty->bg = tty->scheme->palette[parameter - 100 + 8];
  }
}

static void execute_csi(struct tty *tty, unsigned char command)
{
  unsigned parameter = tty->parameters[0];
  unsigned count = parameter ? parameter : 1;
  size_t cell = (size_t)tty->y * tty->width + tty->x;
  size_t cells = (size_t)tty->width * tty->height;

  if (tty->private_csi) {
    if (tty->parameter_index == 0 && parameter == 25 &&
        (command == 'h' || command == 'l')) {
      tty->cursor_visible = command == 'h';
    }
    return;
  }
  if (command == 'm') {
    for (size_t i = 0; i <= tty->parameter_index; ++i) {
      select_style(tty, tty->parameters[i]);
    }
    return;
  }
  if (tty->parameter_index > (command == 'H' ? 1u : 0u)) {
    return;
  }

  if (command == 'A' || command == 'B' || command == 'C' || command == 'D' ||
      command == 'G' || command == 'H' || command == 'J' || command == 'K') {
    tty->wrap_pending = false;
  }

  switch (command) {
  case 'A':
    tty->y = count > tty->y ? 0 : tty->y - count;
    break;
  case 'B':
    tty->y = count >= (unsigned)(tty->height - tty->y) ? tty->height - 1u : tty->y + count;
    break;
  case 'C':
    tty->x = count >= (unsigned)(tty->width - tty->x) ? tty->width - 1u : tty->x + count;
    break;
  case 'D':
    tty->x = count > tty->x ? 0 : tty->x - count;
    break;
  case 'G':
    tty->x = count > tty->width ? tty->width - 1u : count - 1;
    break;
  case 'H': {
    unsigned column = tty->parameters[1] ? tty->parameters[1] : 1;
    tty->y = count > tty->height ? tty->height - 1u : count - 1;
    tty->x = column > tty->width ? tty->width - 1u : column - 1;
    break;
  }
  case 'K':
    if (parameter == 0) {
      erase_cells(tty, cell, (size_t)(tty->y + 1) * tty->width);
    } else if (parameter == 1) {
      erase_cells(tty, (size_t)tty->y * tty->width, cell + 1);
    } else if (parameter == 2) {
      erase_cells(tty, (size_t)tty->y * tty->width, (size_t)(tty->y + 1) * tty->width);
    }
    break;
  case 'J':
    if (parameter == 0) {
      erase_cells(tty, cell, cells);
    } else if (parameter == 1) {
      erase_cells(tty, 0, cell + 1);
    } else if (parameter == 2) {
      erase_cells(tty, 0, cells);
    }
    break;
  }
}

static void begin_escape(struct tty *tty)
{
  tty->escape_state = TTY_ESCAPE;
  tty->parameter_index = 0;
  tty->private_csi = false;
  memset(tty->parameters, 0, sizeof(tty->parameters));
}

void tty_put_char(struct tty *tty, char c)
{
  unsigned char byte = (unsigned char)c;
  if (byte == 0x1b) {
    begin_escape(tty);
    return;
  }
  if (byte == '\n' || byte == '\r' || byte == '\b' || byte == '\t') {
    tty->escape_state = TTY_TEXT;
    tty->wrap_pending = false;
    if (byte == '\n') {
      tty_newline(tty);
    } else if (byte == '\r') {
      tty->x = 0;
    } else if (byte == '\t') {
      unsigned next = tty->x + tty->tab_width - tty->x % tty->tab_width;
      tty->x = next < tty->width ? next : tty->width - 1u;
    } else if (tty->x) {
      --tty->x;
    }
    return;
  }

  if (tty->escape_state == TTY_ESCAPE) {
    tty->escape_state = byte == '[' ? TTY_CSI_ENTRY : TTY_TEXT;
    return;
  }
  if (tty->escape_state == TTY_CSI_ENTRY || tty->escape_state == TTY_CSI ||
      tty->escape_state == TTY_CSI_IGNORE) {
    if (tty->escape_state == TTY_CSI_ENTRY) {
      tty->escape_state = TTY_CSI;
      if (byte == '?') {
        tty->private_csi = true;
        return;
      }
    }
    if (byte >= 0x40 && byte <= 0x7e) {
      if (tty->escape_state == TTY_CSI) {
        execute_csi(tty, byte);
      }
      tty->escape_state = TTY_TEXT;
    } else if (tty->escape_state == TTY_CSI) {
      unsigned value = tty->parameters[tty->parameter_index];
      if (byte >= '0' && byte <= '9' && value * 10 + byte - '0' <= UINT16_MAX) {
        tty->parameters[tty->parameter_index] = value * 10 + byte - '0';
      } else if (byte == ';' && tty->parameter_index + 1 < TTY_CSI_PARAMETERS) {
        ++tty->parameter_index;
      } else {
        /* Unsupported or oversized sequences are discarded through the final
         * byte, rather than displaying their remaining parameters as text. */
        tty->escape_state = TTY_CSI_IGNORE;
      }
    }
    return;
  }
  if (byte < 0x20 || byte == 0x7f) {
    return;
  }

  /* Writing the margin must not scroll a full-screen application's last row.
   * Styles and cursor visibility preserve this pending wrap across writes. */
  if (tty->wrap_pending) {
    tty_newline(tty);
  }
  tty_draw_cell(tty, c, tty->x, tty->y);
  if (tty->x == tty->width - 1) {
    tty->wrap_pending = true;
  } else {
    ++tty->x;
  }
}

void tty_clear(struct tty *tty)
{
  for (size_t y = 0; y < tty->height; ++y) {
    for (size_t x = 0; x < tty->width; ++x) {
      tty_plot_char(tty, ' ', x, y, tty->fg, tty->bg);
    }
  }
  tty->x = 0;
  tty->y = 0;
  tty->escape_state = TTY_TEXT;
  tty->wrap_pending = false;
}

void tty_fresh_line(struct tty *tty)
{
  tty->escape_state = TTY_TEXT;
  if (tty->x || tty->wrap_pending) {
    tty_newline(tty);
  }
}

void tty_resize(struct tty *tty, const struct framebuffer *fb, uint8_t *cells)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  size_t width = fb->width / tty->font->width;
  size_t height = fb->height / tty->font->height;
  KASSERT(width && width <= UINT16_MAX && height && height <= UINT16_MAX);
  KASSERT(width <= SIZE_MAX / height);
  KASSERT(cells && tty->cells && cells != tty->cells);
  KASSERT(tty->geometry_generation && tty->geometry_generation < UINT64_MAX);

  size_t first_row = tty->y >= height ? tty->y - height + 1 : 0;
  size_t rows = MIN((size_t)tty->height - first_row, height);
  size_t columns = MIN((size_t)tty->width, width);
  uint32_t background = framebuffer_color(fb, tty->bg);
  for (size_t y = 0; y < fb->height; ++y) {
    uint32_t *pixels = (uint32_t *)(fb->address + y * fb->pitch);
    for (size_t x = 0; x < fb->width; ++x) {
      pixels[x] = background;
    }
  }

  size_t source_y = first_row * tty->font->height;
  size_t row_bytes = columns * tty->font->width * sizeof(uint32_t);
  for (size_t y = 0; y < rows * tty->font->height; ++y) {
    memcpy((void *)(fb->address + y * fb->pitch),
        (const void *)(tty->fb->address + (source_y + y) * tty->fb->pitch), row_bytes);
  }
  memset(cells, ' ', width * height);
  for (size_t y = 0; y < rows; ++y) {
    memcpy(cells + y * width, tty->cells + (first_row + y) * tty->width, columns);
  }

  tty_selection_clear(tty);
  tty->fb = fb;
  tty->cells = cells;
  tty->width = width;
  tty->height = height;
  tty->x = MIN((size_t)tty->x, width - 1);
  tty->y = MIN((size_t)tty->y - first_row, height - 1);
  tty->wrap_pending = false;
  ++tty->geometry_generation;
}

void tty_selection_clear(struct tty *tty)
{
  tty->selection_anchor = 0;
  tty->selection_endpoint = 0;
  tty->selection_valid = false;
  tty->selection_dragging = false;
}

void tty_selection_cancel_drag(struct tty *tty)
{
  if (tty->selection_dragging) {
    tty_selection_clear(tty);
  }
}

static size_t selection_coordinate(int64_t coordinate, size_t pixels, size_t cells)
{
  if (coordinate < 0) {
    return 0;
  }
  return MIN((uint64_t)coordinate / pixels, cells - 1);
}

void tty_selection_input(struct tty *tty, int64_t x, int64_t y, bool pressed, bool held)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!tty->cells) {
    return;
  }
  KASSERT(tty->width && tty->height && tty->font->width && tty->font->height);
  KASSERT(tty->width <= SIZE_MAX / tty->font->width &&
      tty->height <= SIZE_MAX / tty->font->height);
  if (pressed) {
    tty_selection_clear(tty);
    size_t pixel_width = (size_t)tty->width * tty->font->width;
    size_t pixel_height = (size_t)tty->height * tty->font->height;
    if (x < 0 || y < 0 || (uint64_t)x >= pixel_width || (uint64_t)y >= pixel_height) {
      return;
    }
    size_t column = (uint64_t)x / tty->font->width;
    size_t row = (uint64_t)y / tty->font->height;
    tty->selection_anchor = row * tty->width + column;
    tty->selection_endpoint = tty->selection_anchor;
    tty->selection_dragging = true;
  }
  if (!tty->selection_dragging) {
    return;
  }
  size_t column = selection_coordinate(x, tty->font->width, tty->width);
  size_t row = selection_coordinate(y, tty->font->height, tty->height);
  size_t endpoint = row * tty->width + column;
  bool same_cell = x >= 0 && y >= 0 &&
      (uint64_t)x / tty->font->width == tty->selection_anchor % tty->width &&
      (uint64_t)y / tty->font->height == tty->selection_anchor / tty->width;
  if (held && !same_cell) {
    tty->selection_valid = true;
  }
  if (tty->selection_valid) {
    tty->selection_endpoint = endpoint;
  }
  if (!held) {
    if (tty->selection_valid) {
      tty->selection_dragging = false;
    } else {
      tty_selection_clear(tty);
    }
  }
}

bool tty_selection_row(const struct tty *tty, size_t row, size_t *first, size_t *last)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!tty->cells || !tty->selection_valid || row >= tty->height) {
    return false;
  }
  size_t begin = MIN(tty->selection_anchor, tty->selection_endpoint);
  size_t end = MAX(tty->selection_anchor, tty->selection_endpoint);
  size_t row_begin = row * tty->width;
  size_t row_end = row_begin + tty->width - 1;
  if (begin > row_end || end < row_begin) {
    return false;
  }
  *first = MAX(begin, row_begin) - row_begin;
  *last = MIN(end, row_end) - row_begin;
  return true;
}
