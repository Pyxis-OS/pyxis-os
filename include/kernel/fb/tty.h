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
  TTY_CHARSET, /* ESC ( ) * or +: the next byte names a character set. */
  TTY_CSI_ENTRY,
  TTY_CSI,
  TTY_CSI_IGNORE,
};

#define TTY_CSI_PARAMETERS 4
#define TTY_DEFAULT_TAB_WIDTH 8

/* A cell's stored style: palette indices 0..15, or TTY_DEFAULT_COLOR for the
 * scheme's separate foreground/background, plus reverse video. */
#define TTY_DEFAULT_COLOR 16u
#define TTY_STYLE_FOREGROUND_MASK 0x001fu
#define TTY_STYLE_BACKGROUND_SHIFT 5
#define TTY_STYLE_BACKGROUND_MASK 0x03e0u
#define TTY_STYLE_REVERSE 0x0400u

/* Bytes of cell storage for a width * height screen: glyphs and styles for
 * the primary and alternate screens. */
#define TTY_STORAGE_BYTES_PER_CELL (2 * (sizeof(uint8_t) + sizeof(uint16_t)))

/* DECSC state, one per screen. */
struct tty_saved_cursor
{
  uint16_t x;
  uint16_t y;
  uint8_t foreground;
  uint8_t background;
  bool reverse;
  bool wrap_pending;
};

struct tty
{
  uint16_t x;
  uint16_t y;

  uint16_t width;
  uint16_t height;
  uint16_t tab_width; /* Nonzero; changes share the TTY output lock. */
  uint64_t geometry_generation; /* Starts at one; output lock protects geometry. */

  /* Colors use 0xRRGGBB, independent of the framebuffer channel layout.
   * They follow the palette indices, which cells record. */
  uint32_t fg;
  uint32_t bg;
  uint8_t foreground;
  uint8_t background;

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
  /* Scroll region rows, inclusive; the full screen unless DECSTBM narrows it. */
  uint16_t region_top;
  uint16_t region_bottom;
  /* The alternate screen is showing. Its cells and the primary's swap with
   * other_cells/other_styles; the framebuffer holds only the active screen. */
  bool alternate;
  struct tty_saved_cursor saved[2]; /* Indexed by alternate. */

  /* Local TTYs attach checked, BSP-allocated storage of
   * width * height * TTY_STORAGE_BYTES_PER_CELL bytes before initialization;
   * raw/early drawing needs none and has no alternate screen. cells holds the
   * active screen's packed glyphs and styles its cell styles. The output lock
   * protects cells, styles, selection and raster changes together. */
  uint8_t *storage;
  uint8_t *cells;
  uint16_t *styles;
  uint8_t *other_cells;
  uint16_t *other_styles;
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

/* Attach STORAGE (see struct tty) and clear both screens. */
void tty_attach_storage(struct tty *tty, uint8_t *storage);
void tty_put_char(struct tty *tty, char c);
void tty_clear(struct tty *tty);
void tty_fresh_line(struct tty *tty);
/* Output lock, IF=0. FB and STORAGE are disjoint preallocated backing; STORAGE
 * has (fb->width / font->width) * (fb->height / font->height) *
 * TTY_STORAGE_BYTES_PER_CELL bytes. Copies whole cells of both screens without
 * reflow, keeps the cursor visible and publishes raster/text geometry
 * together. Resets the scroll region and clears selection; the caller retires
 * the old tty->storage. */
void tty_resize(struct tty *tty, const struct framebuffer *fb, uint8_t *storage);

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
