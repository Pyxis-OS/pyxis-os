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

static uint32_t framebuffer_color(const struct framebuffer *fb, uint32_t rgb)
{
  uint8_t red = rgb >> 16;
  uint8_t green = rgb >> 8;
  uint8_t blue = rgb;

  return ((uint32_t)red << fb->red_shift) |
         ((uint32_t)green << fb->green_shift) |
         ((uint32_t)blue << fb->blue_shift);
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

void tty_put_char(struct tty *tty, char c)
{
  if (c == '\n') {
    tty_newline(tty);
    return;
  }

  tty_plot_char(tty, c, tty->x, tty->y, tty->fg, tty->bg);

  tty->x++;
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
}
