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
  TTY_CSI_ENTRY,
  TTY_CSI,
  TTY_CSI_IGNORE,
};

#define TTY_CSI_PARAMETERS 4
#define TTY_DEFAULT_TAB_WIDTH 8

struct tty
{
  uint16_t x;
  uint16_t y;

  uint16_t width;
  uint16_t height;
  uint16_t tab_width; /* Nonzero; changes share the TTY output lock. */
  uint64_t geometry_generation; /* Starts at one; output lock protects geometry. */

  /* Colors use 0xRRGGBB, independent of the framebuffer channel layout. */
  uint32_t fg;
  uint32_t bg;

  bool initialized;
  bool reverse;
  bool wrap_pending;
  bool cursor_visible;

  /* Output calls can split a sequence; parser state belongs to the TTY. */
  enum tty_escape_state escape_state;
  uint16_t parameters[TTY_CSI_PARAMETERS];
  size_t parameter_index;
  bool private_csi;

  const struct font *font;
  const struct color_scheme *scheme;
  const struct framebuffer *fb;
  /* Packed visible glyphs, width * height bytes. Local TTYs attach checked,
   * BSP-allocated storage before initialization; raw/early drawing needs none.
   * The output lock protects cells, selection and raster changes together. */
  uint8_t *cells;
  size_t selection_anchor;
  size_t selection_endpoint;
  bool selection_valid;
  bool selection_dragging;
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
void tty_fresh_line(struct tty *tty);
/* Output lock, IF=0. FB and CELLS are disjoint preallocated backing; CELLS has
 * (fb->width / font->width) * (fb->height / font->height) bytes. Copies whole
 * cells without reflow, keeps the cursor visible and publishes raster/text
 * geometry together. Clears selection; caller retires the old backing. */
void tty_resize(struct tty *tty, const struct framebuffer *fb, uint8_t *cells);

/* Output lock, IF=0. Coordinates are content-local pixels. Only a fresh left
 * press in the whole-cell grid starts selection; an anchored held drag clamps
 * to its edges. Release finalizes. No allocation or clipboard publication. */
void tty_selection_input(struct tty *tty, int64_t x, int64_t y, bool pressed, bool held);
/* Cancel an unfinished selection, preserving a completed one. */
void tty_selection_cancel_drag(struct tty *tty);
void tty_selection_clear(struct tty *tty);
/* Output lock, IF=0. Returns inclusive selected columns for one visible row.
 * The caller stages tty->cells while holding that lock, then composes outside. */
bool tty_selection_row(const struct tty *tty, size_t row, size_t *first, size_t *last);


#endif // KERNEL_FB_TTY_H
