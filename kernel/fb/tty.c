//
// Created by chronium on 9/13/26.
//

#include <kernel/fb/tty.h>
#include <kernel/memory.h>
#include <arch/cpu.h>

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
  return &global_tty;
}

void tty_init(const struct boot_framebuffer *fb,
              const struct color_scheme *scheme, const struct font *font)
{
  get_tty()->x = 0;
  get_tty()->y = 0;

  get_tty()->width = fb->width / font->width;
  get_tty()->height = fb->height / font->height;

  get_tty()->fg = scheme->foreground;
  get_tty()->bg = scheme->background;

  get_tty()->font = font;
  get_tty()->scheme = scheme;
  get_tty()->fb = fb;

  tty_clear();

  get_tty()->initialized = true;
}

void tty_plot_char(struct tty *tty, char c, uint16_t x, uint16_t y,
               uint32_t fg, uint32_t bg)
{
  const struct font *font = tty->font;
  if (c < 0 || c > font->max_glyph) {
    c = '?';
  }

  uint16_t x_dst = x * font->width;
  uint16_t y_dst = y * font->height;

  const uint8_t *glyph =
      font->data + (size_t)(unsigned char)c * font->stride;

  for (size_t row = 0; row < font->height; ++row) {
    volatile uint32_t *pixel_row =
        (volatile uint32_t *)((uint8_t *)tty->fb->address +
                              (y_dst + row) * tty->fb->pitch);

    for (size_t col = 0; col < font->width; ++col) {
      bool bit =
          (glyph[row] >>
           (col % 8)) & 1;

      pixel_row[x_dst + col] = bit ? fg : bg;
    }
  }

  cpu_store_fence();
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

void tty_put_char(char c)
{
  struct tty *tty = get_tty();

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

void tty_clear(void)
{
  struct tty *tty = get_tty();
  for (size_t y = 0; y < tty->height; ++y) {
    for (size_t x = 0; x < tty->width; ++x) {
      tty_plot_char(tty, ' ', x, y, tty->fg, tty->bg);
    }
  }
  tty->x = 0;
  tty->y = 0;
}
