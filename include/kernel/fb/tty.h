//
// Created by chronium on 9/13/26.
//

#ifndef KERNEL_FB_TTY_H
#define KERNEL_FB_TTY_H

#include <stdint.h>

#include "font.h"
#include "kernel/boot.h"

struct tty
{
  uint16_t x;
  uint16_t y;

  uint16_t width;
  uint16_t height;

  /* Colors use 0xRRGGBB, independent of the framebuffer channel layout. */
  uint32_t fg;
  uint32_t bg;

  bool initialized;

  const struct font *font;
  const struct color_scheme *scheme;
  const struct boot_framebuffer *fb;
};

/* Scheme and drawing colors use 0xRRGGBB. */
struct color_scheme
{
  uint32_t palette[16];

  uint32_t foreground;
  uint32_t background;

  uint32_t cursor;
  uint32_t cursor_text;

  uint32_t selection;
  uint32_t selection_background;
};

extern const struct color_scheme aardvark_scheme;

void tty_init(const struct boot_framebuffer *fb,
  const struct color_scheme *scheme, const struct font *font);

void tty_plot_char(struct tty *tty, char c, uint16_t x, uint16_t y,
  uint32_t fg, uint32_t bg);

void tty_put_char(struct tty *tty, char c);
void tty_clear(void);

struct tty *get_tty(void);

#endif // KERNEL_FB_TTY_H
