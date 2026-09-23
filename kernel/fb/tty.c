//
// Created by chronium on 9/13/26.
//

#include <kernel/fb/tty.h>
#include <kernel/memory.h>
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
    .selection = 0xb4bcca,
    .selection_background = 0x0f141f
};

struct tty global_tty = {0};

struct tty *get_tty(void)
{
  return cpu_current()->space->tty;
}

void tty_plot_char_raw(const struct framebuffer *fb, const struct font *font,
    char c, size_t x, size_t y, uint32_t fg, uint32_t bg)
{
  unsigned char glyph_index = (unsigned char)c;
  if (glyph_index > font->max_glyph) {
    glyph_index = '?';
  }

  const uint8_t *glyph =
      font->data + (size_t)glyph_index * font->stride;
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

  uint16_t x_dst = x * font->width;
  uint16_t y_dst = y * font->height;

  tty_plot_char_raw(tty->fb, font, c, x_dst, y_dst, fg, bg);
}

static void tty_newline(struct tty *tty)
{
  tty->x = 0;
  tty->y++;

  if (tty->y >= tty->height) {
    size_t row_bytes = tty->fb->pitch * tty->font->height;
    void *pixels = (void *)tty->fb->address;

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

  if (command == 'm') {
    for (size_t i = 0; i <= tty->parameter_index; ++i) {
      select_style(tty, tty->parameters[i]);
    }
    return;
  }
  if (tty->parameter_index > (command == 'H' ? 1u : 0u)) {
    return;
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
  memset(tty->parameters, 0, sizeof(tty->parameters));
}

void tty_put_char(struct tty *tty, char c)
{
  unsigned char byte = (unsigned char)c;
  if (byte == 0x1b) {
    begin_escape(tty);
    return;
  }
  if (byte == '\n' || byte == '\r' || byte == '\b') {
    tty->escape_state = TTY_TEXT;
    if (byte == '\n') {
      tty_newline(tty);
    } else if (byte == '\r') {
      tty->x = 0;
    } else if (tty->x) {
      --tty->x;
    }
    return;
  }

  if (tty->escape_state == TTY_ESCAPE) {
    tty->escape_state = byte == '[' ? TTY_CSI : TTY_TEXT;
    return;
  }
  if (tty->escape_state == TTY_CSI || tty->escape_state == TTY_CSI_IGNORE) {
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

  tty_draw_cell(tty, c, tty->x, tty->y);
  ++tty->x;
  if (tty->x >= tty->width) {
    tty_newline(tty);
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
}

void tty_fresh_line(struct tty *tty)
{
  tty->escape_state = TTY_TEXT;
  if (tty->x) {
    tty_newline(tty);
  }
}
