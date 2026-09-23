//
// Created by chronium on 9/13/26.
//

#ifndef KERNEL_FB_TTY_H
#define KERNEL_FB_TTY_H

#include <stdint.h>
#include <kernel/fb/fb.h>

#include "font.h"

enum tty_escape_state {
  TTY_TEXT,
  TTY_ESCAPE,
  TTY_CSI,
  TTY_CSI_IGNORE,
};

#define TTY_CSI_PARAMETERS 4

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
  bool reverse;

  /* Output calls can split a sequence; parser state belongs to the TTY. */
  enum tty_escape_state escape_state;
  uint16_t parameters[TTY_CSI_PARAMETERS];
  size_t parameter_index;

  const struct font *font;
  const struct color_scheme *scheme;
  const struct framebuffer *fb;
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

void tty_plot_char_raw(const struct framebuffer *fb, const struct font *font,
    char c, size_t x, size_t y, uint32_t fg, uint32_t bg);

void tty_plot_char(struct tty *tty, char c, uint16_t x, uint16_t y,
  uint32_t fg, uint32_t bg);

void tty_put_char(struct tty *tty, char c);
void tty_clear(struct tty *tty);

struct tty *get_tty(void);

#endif // KERNEL_FB_TTY_H
