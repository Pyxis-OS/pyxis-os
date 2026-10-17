//
// Created by chronium on 9/13/26.
//

#ifndef KERNEL_FB_TTY_H
#define KERNEL_FB_TTY_H

#include <stdatomic.h>
#include <stdint.h>
#include <kernel/fb/fb.h>
#include <terminal/style.h>
#include <terminal/text.h>

#include "font.h"

enum tty_escape_state {
  TTY_TEXT,
  TTY_ESCAPE,
  TTY_CHARSET, /* ESC ( ) * or +: the next byte names a character set. */
  TTY_CSI_ENTRY,
  TTY_CSI,
  TTY_CSI_IGNORE,
};

#define TTY_CSI_PARAMETERS TERMINAL_CSI_PARAMETERS
#define TTY_DEFAULT_TAB_WIDTH 8

/* Bytes of cell storage per screen cell: the primary and alternate screens. */
#define TTY_STORAGE_BYTES_PER_CELL (2 * sizeof(struct terminal_cell))

/* DECSC state, one per screen. */
struct tty_saved_cursor
{
  uint16_t x;
  uint16_t y;
  struct terminal_style style;
  bool wrap_pending;
};

/* Writers change cells only. The presenter collects the changed rows under the
 * output lock and draws them into tty->fb without it; only the presenter
 * touches that raster, including on resize. */
struct tty
{
  /* Output lock (tty_lock()): IF=0, never held across logging, allocation or
   * another TTY's lock. The log lock may be held while taking it. */
  atomic_bool output_locked;

  uint16_t x;
  uint16_t y;

  uint16_t width;
  uint16_t height;
  uint16_t tab_width; /* Nonzero; changes share the TTY output lock. */
  uint64_t geometry_generation; /* Starts at one; output lock protects geometry. */
  uint64_t visual_generation; /* Output lock; cell, caret and selection changes. */
  uint64_t background_generation; /* Output lock; exposed graphics margin color. */

  struct terminal_style style;

  bool initialized;
  bool wrap_pending;
  bool cursor_visible;

  /* Output calls can split a sequence; parser state belongs to the TTY. */
  enum tty_escape_state escape_state;
  struct terminal_utf8 utf8;
  uint16_t parameters[TTY_CSI_PARAMETERS];
  uint16_t parameters_present;
  size_t parameter_index;
  bool private_csi;

  const struct font *font;
  const struct color_scheme *scheme;
  const struct framebuffer *fb;
  /* Scroll region rows, inclusive; the full screen unless DECSTBM narrows it. */
  uint16_t region_top;
  uint16_t region_bottom;
  /* The alternate screen is showing. Its cells and the primary's swap with
   * other_cells; the framebuffer holds only the active screen. */
  bool alternate;
  struct tty_saved_cursor saved[2]; /* Indexed by alternate. */

  /* TTYs attach checked, BSP-allocated storage of tty_storage_bytes() before
   * initialization. Each screen's rows form a ring: row y of the screen is
   * storage row (origin + y) % height, so scrolling the whole screen rotates
   * origin instead of moving cells. The output lock protects cells, origins,
   * selection and the presenter handoff below. */
  uint8_t *storage;
  struct terminal_cell *cells;
  struct terminal_cell *other_cells;
  uint16_t origin;
  uint16_t other_origin;
  /* Presenter handoff: screen rows changed since the last collection, the net
   * number of whole-screen rows scrolled up (negative: down) that the raster
   * has not applied, and whether the raster must be redrawn entirely. */
  uint8_t *dirty;
  int32_t scrolled;
  bool redraw;
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

/* Presenter-owned staging for one collection, sized for the screen's cell
 * geometry: the cells of each row to draw, in screen order. */
struct tty_render
{
  struct terminal_cell *cells;
  uint8_t *rows;
  size_t cell_capacity;
  size_t row_capacity;
  int32_t scrolled;
  bool redraw;
  uint32_t background; /* 0xRRGGBB for margins on a full redraw. */
};

void tty_plot_char_raw(const struct framebuffer *fb, const struct font *font,
    uint16_t glyph, size_t x, size_t y, uint32_t fg, uint32_t bg);
void tty_plot_char_styled(const struct framebuffer *fb, const struct font *font,
    uint16_t glyph, size_t x, size_t y, uint32_t fg, uint32_t bg, uint8_t attributes);

/* Storage bytes for a WIDTH * HEIGHT cell screen: both screens' cells and the
 * changed-row flags. */
size_t tty_storage_bytes(size_t width, size_t height);
/* Attach STORAGE (see struct tty) and clear both screens. */
void tty_attach_storage(struct tty *tty, uint8_t *storage);
/* IF=0. False only once panic output has started; then the lock is not held. */
bool tty_lock(struct tty *tty);
void tty_unlock(struct tty *tty);
/* Output lock. The cells of screen row ROW. */
const struct terminal_cell *tty_row(const struct tty *tty, size_t row);
/* Presenter, output lock. Copy the rows to draw into RENDER and clear the
 * handoff. False when nothing changed. */
bool tty_render_collect(struct tty *tty, struct tty_render *render);
/* Presenter, without the output lock: geometry, font and scheme change only in
 * the presenter. Applies the collected scroll to tty->fb and draws the rows. */
void tty_render_draw(const struct tty *tty, const struct tty_render *render);
void tty_put_char(struct tty *tty, char c);
void tty_clear(struct tty *tty);
void tty_fresh_line(struct tty *tty);
/* Presenter, output lock, IF=0. FB and STORAGE are disjoint preallocated
 * backing; STORAGE has tty_storage_bytes() for fb's cell geometry. Copies whole
 * cells of both screens without reflow, keeps the cursor visible and publishes
 * the new geometry; the presenter redraws the new raster. Resets the scroll
 * region and clears selection; the caller retires the old tty->storage. */
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
