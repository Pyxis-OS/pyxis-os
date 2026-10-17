//
// Created by chronium on 9/13/26.
//

#include <kernel/fb/tty.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>

const struct color_scheme aardvark_scheme = {
    .palette = TERMINAL_AARDVARK_PALETTE,
    .foreground = TERMINAL_AARDVARK_FOREGROUND,
    .background = TERMINAL_AARDVARK_BACKGROUND,
    .cursor = TERMINAL_AARDVARK_FOREGROUND,
    .cursor_text = TERMINAL_AARDVARK_BACKGROUND,
    .selection = TERMINAL_AARDVARK_BACKGROUND,
    .selection_background = TERMINAL_AARDVARK_FOREGROUND
};

struct tty global_tty = {.geometry_generation = 1};

static const uint8_t *glyph_bits(const struct font *font, uint16_t index)
{
  return font_glyph(font, index <= font->max_glyph ? index : '?');
}

void tty_plot_char_raw(const struct framebuffer *fb, const struct font *font,
    uint16_t index, size_t x, size_t y, uint32_t fg, uint32_t bg)
{
  const uint8_t *glyph = glyph_bits(font, index);
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

void tty_plot_char_styled(const struct framebuffer *fb, const struct font *font,
    uint16_t index, size_t x, size_t y, uint32_t fg, uint32_t bg, uint8_t attributes)
{
  if (!(attributes & (TERMINAL_ATTR_BOLD | TERMINAL_ATTR_ITALIC | TERMINAL_ATTR_UNDERLINE))) {
    tty_plot_char_raw(fb, font, index, x, y, fg, bg);
    return;
  }
  const uint8_t *glyph = glyph_bits(font, index);
  uint32_t foreground = framebuffer_color(fb, fg);
  uint32_t background = framebuffer_color(fb, bg);

  for (size_t row = 0; row < font->height; ++row) {
    uint8_t bits = glyph[row];
    if (attributes & TERMINAL_ATTR_ITALIC) {
      unsigned shift = 2 - row * 3 / font->height;
      bits = (uint8_t)(bits << shift);
    }
    if (attributes & TERMINAL_ATTR_BOLD) {
      bits |= (uint8_t)(bits << 1);
    }
    if ((attributes & TERMINAL_ATTR_UNDERLINE) && row == font->height - 1) {
      bits = UINT8_MAX;
    }
    volatile uint32_t *pixel_row =
        (volatile uint32_t *)((uint8_t *)fb->address + (y + row) * fb->pitch);
    for (size_t col = 0; col < font->width; ++col) {
      pixel_row[x + col] = ((bits >> (col % 8)) & 1) ? foreground : background;
    }
  }
  cpu_store_fence();
}

static struct terminal_cell styled_cell(const struct tty *tty, uint16_t character, uint8_t attributes)
{
  return (struct terminal_cell){
    .foreground = tty->style.foreground,
    .background = tty->style.background,
    .character = character,
    .attributes = attributes,
  };
}

static void draw_cell(const struct tty *tty, struct terminal_cell cell, size_t x, size_t y)
{
  uint32_t fg = terminal_color_rgb(cell.foreground, tty->scheme->palette,
      tty->scheme->foreground);
  uint32_t bg = terminal_color_rgb(cell.background, tty->scheme->palette,
      tty->scheme->background);
  if (cell.attributes & TERMINAL_ATTR_REVERSE) {
    uint32_t swap = fg;
    fg = bg;
    bg = swap;
  }
  tty_plot_char_styled(tty->fb, tty->font, terminal_character_glyph(cell.character), x * tty->font->width,
      y * tty->font->height, fg, bg, cell.attributes);
}

static struct terminal_cell *row_cells(struct tty *tty, size_t y)
{
  return tty->cells + (size_t)(tty->origin + y) % tty->height * tty->width;
}

const struct terminal_cell *tty_row(const struct tty *tty, size_t row)
{
  KASSERT(row < tty->height);
  return tty->cells + (size_t)(tty->origin + row) % tty->height * tty->width;
}

/* Record one cell of the active screen for the presenter. */
static void put_cell(struct tty *tty, struct terminal_cell cell, size_t x, size_t y)
{
  KASSERT(tty->cells && x < tty->width && y < tty->height);
  struct terminal_cell *slot = row_cells(tty, y) + x;
  struct terminal_cell previous = *slot;
  bool changed = previous.character != cell.character ||
      previous.foreground != cell.foreground ||
      previous.background != cell.background ||
      previous.attributes != cell.attributes;
  if ((tty->selection_valid || tty->selection_dragging) &&
      previous.character != cell.character) {
    size_t index = y * tty->width + x;
    size_t first = MIN(tty->selection_anchor, tty->selection_endpoint);
    size_t last = MAX(tty->selection_anchor, tty->selection_endpoint);
    if (index >= first && index <= last) {
      tty_selection_clear(tty);
    }
  }
  *slot = cell;
  if (changed) {
    tty->dirty[y] = 1;
    ++tty->visual_generation;
  }
}

static void blank_rows(struct tty *tty, size_t first, size_t end)
{
  struct terminal_cell blank = styled_cell(tty, ' ', 0);
  for (size_t y = first; y < end; ++y) {
    for (size_t x = 0; x < tty->width; ++x) {
      put_cell(tty, blank, x, y);
    }
  }
}

static void redraw_all(struct tty *tty)
{
  tty->redraw = true;
  tty->scrolled = 0;
  ++tty->visual_generation;
}

/* The raster can follow by moving pixels while the net scroll stays within
 * the screen; beyond that every row is drawn again. */
static void note_scroll(struct tty *tty, int32_t rows)
{
  if (tty->redraw) {
    return;
  }
  int64_t scrolled = (int64_t)tty->scrolled + rows;
  if (scrolled >= tty->height || -scrolled >= tty->height) {
    redraw_all(tty);
    return;
  }
  tty->scrolled = (int32_t)scrolled;
  ++tty->visual_generation;
}

/* Move rows [source, source + count) to destination by copying cells. */
static void move_rows(struct tty *tty, size_t destination, size_t source, size_t count)
{
  if (!count || destination == source) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    /* Copy away from the overlap, as memmove would. */
    size_t row = destination < source ? i : count - 1 - i;
    memcpy(row_cells(tty, destination + row), row_cells(tty, source + row),
        tty->width * sizeof(*tty->cells));
    tty->dirty[destination + row] = 1;
  }
  ++tty->visual_generation;
}

static bool whole_screen(const struct tty *tty, size_t top, size_t bottom)
{
  return top == 0 && bottom == tty->height - 1u;
}

/* Scroll rows top..bottom (inclusive) up by count, blanking the bottom. The
 * whole screen rotates the row ring; a narrower region copies its rows. */
static void scroll_up(struct tty *tty, size_t top, size_t bottom, size_t count)
{
  size_t rows = bottom - top + 1;
  count = MIN(count, rows);
  tty_selection_clear(tty);
  bool rotate = whole_screen(tty, top, bottom);
  if (rotate) {
    tty->origin = (uint16_t)((tty->origin + count) % tty->height);
    memmove(tty->dirty, tty->dirty + count, rows - count);
    note_scroll(tty, (int32_t)count);
  } else {
    move_rows(tty, top, top + count, rows - count);
  }
  blank_rows(tty, bottom + 1 - count, bottom + 1);
  if (rotate) {
    /* The raster moves too, so these rows show whatever scrolled off. */
    memset(tty->dirty + bottom + 1 - count, 1, count);
  }
}

/* Scroll rows top..bottom (inclusive) down by count, blanking the top. */
static void scroll_down(struct tty *tty, size_t top, size_t bottom, size_t count)
{
  size_t rows = bottom - top + 1;
  count = MIN(count, rows);
  tty_selection_clear(tty);
  bool rotate = whole_screen(tty, top, bottom);
  if (rotate) {
    tty->origin = (uint16_t)((tty->origin + tty->height - count) % tty->height);
    memmove(tty->dirty + count, tty->dirty, rows - count);
    note_scroll(tty, -(int32_t)count);
  } else {
    move_rows(tty, top + count, top, rows - count);
  }
  blank_rows(tty, top, top + count);
  if (rotate) {
    memset(tty->dirty + top, 1, count);
  }
}

/* LF: the bottom margin scrolls the region; below it the screen edge stops. */
static void tty_newline(struct tty *tty)
{
  tty->wrap_pending = false;
  tty->x = 0;
  if (tty->y == tty->region_bottom) {
    scroll_up(tty, tty->region_top, tty->region_bottom, 1);
  } else if (tty->y + 1u < tty->height) {
    tty->y++;
  }
}

/* ESC M: the top margin scrolls the region down; above it the screen edge stops. */
static void reverse_index(struct tty *tty)
{
  tty->wrap_pending = false;
  if (tty->y == tty->region_top) {
    scroll_down(tty, tty->region_top, tty->region_bottom, 1);
  } else if (tty->y) {
    tty->y--;
  }
}

static void tty_draw_cell(struct tty *tty, uint16_t character, size_t x, size_t y)
{
  put_cell(tty, styled_cell(tty, character, tty->style.attributes), x, y);
}

static void erase_cells(struct tty *tty, size_t first, size_t end)
{
  struct terminal_cell blank = styled_cell(tty, ' ', 0);
  for (size_t cell = first; cell < end; ++cell) {
    put_cell(tty, blank, cell % tty->width, cell / tty->width);
  }
}

static void save_cursor(struct tty *tty)
{
  tty->saved[tty->alternate] = (struct tty_saved_cursor){
    .x = tty->x,
    .y = tty->y,
    .style = tty->style,
    .wrap_pending = tty->wrap_pending,
  };
}

static void restore_cursor(struct tty *tty)
{
  const struct tty_saved_cursor *saved = &tty->saved[tty->alternate];
  tty->x = MIN(saved->x, tty->width - 1u);
  tty->y = MIN(saved->y, tty->height - 1u);
  tty->style = saved->style;
  tty->wrap_pending = saved->wrap_pending && tty->x == tty->width - 1u;
}

static void reset_saved_cursor(struct tty_saved_cursor *saved)
{
  *saved = (struct tty_saved_cursor){
    .style = {.foreground = TERMINAL_COLOR_DEFAULT, .background = TERMINAL_COLOR_DEFAULT},
  };
}

static void reset_region(struct tty *tty)
{
  tty->region_top = 0;
  tty->region_bottom = tty->height - 1u;
}

/* CSI ? 1049 h/l. Entering saves the primary cursor and shows a cleared
 * alternate screen; leaving shows the primary's cells again and restores the
 * cursor. The scroll region resets either way. */
static void select_screen(struct tty *tty, bool alternate)
{
  if (!tty->storage || tty->alternate == alternate) {
    return;
  }
  if (alternate) {
    save_cursor(tty);
  }
  struct terminal_cell *cells = tty->cells;
  tty->cells = tty->other_cells;
  tty->other_cells = cells;
  uint16_t origin = tty->origin;
  tty->origin = tty->other_origin;
  tty->other_origin = origin;
  tty->alternate = alternate;
  redraw_all(tty);
  tty_selection_clear(tty);
  reset_region(tty);
  tty->wrap_pending = false;
  if (alternate) {
    blank_rows(tty, 0, tty->height);
    tty->x = 0;
    tty->y = 0;
  } else {
    restore_cursor(tty);
  }
}

static bool cursor_in_region(const struct tty *tty)
{
  return tty->y >= tty->region_top && tty->y <= tty->region_bottom;
}

static void execute_private_csi(struct tty *tty, unsigned char command)
{
  unsigned parameter = tty->parameters[0];
  if (tty->parameter_index != 0 || (command != 'h' && command != 'l')) {
    return;
  }
  if (parameter == 25) {
    tty->cursor_visible = command == 'h';
  } else if (parameter == 1049) {
    select_screen(tty, command == 'h');
  }
}

static void execute_csi(struct tty *tty, unsigned char command)
{
  unsigned parameter = tty->parameters[0];
  unsigned count = parameter ? parameter : 1;
  size_t cell = (size_t)tty->y * tty->width + tty->x;
  size_t cells = (size_t)tty->width * tty->height;

  if (tty->private_csi) {
    execute_private_csi(tty, command);
    return;
  }
  if (command == 'm') {
    terminal_sgr_apply(&tty->style, tty->parameters, tty->parameters_present,
        tty->parameter_index + 1);
    return;
  }
  if (command == 'r') {
    /* DECSTBM: one-based rows; zero or missing selects the screen edge. */
    if (tty->parameter_index > 1) {
      return;
    }
    unsigned top = parameter ? parameter : 1;
    unsigned bottom = tty->parameters[1] ? tty->parameters[1] : tty->height;
    if (top < bottom && bottom <= tty->height) {
      tty->region_top = top - 1;
      tty->region_bottom = bottom - 1;
      tty->x = 0;
      tty->y = 0;
      tty->wrap_pending = false;
    }
    return;
  }
  if (tty->parameter_index > (command == 'H' ? 1u : 0u)) {
    return;
  }
  if ((command == 's' || command == 'u') && parameter) {
    return;
  }

  if (command == 'A' || command == 'B' || command == 'C' || command == 'D' ||
      command == 'G' || command == 'H' || command == 'J' || command == 'K' ||
      command == 'L' || command == 'M') {
    tty->wrap_pending = false;
  }

  switch (command) {
  case 'A': {
    /* Inside the region the top margin stops the cursor, as in xterm. */
    unsigned limit = cursor_in_region(tty) ? tty->region_top : 0;
    tty->y = count > (unsigned)(tty->y - limit) ? limit : tty->y - count;
    break;
  }
  case 'B': {
    unsigned limit = cursor_in_region(tty) ? tty->region_bottom : tty->height - 1u;
    tty->y = count > (unsigned)(limit - tty->y) ? limit : tty->y + count;
    break;
  }
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
  case 'L':
    /* Insert and delete lines act inside the region and return to column one. */
    if (cursor_in_region(tty)) {
      scroll_down(tty, tty->y, tty->region_bottom, count);
      tty->x = 0;
    }
    break;
  case 'M':
    if (cursor_in_region(tty)) {
      scroll_up(tty, tty->y, tty->region_bottom, count);
      tty->x = 0;
    }
    break;
  case 's':
    save_cursor(tty);
    break;
  case 'u':
    restore_cursor(tty);
    break;
  }
}

static void begin_escape(struct tty *tty)
{
  tty->escape_state = TTY_ESCAPE;
  tty->parameter_index = 0;
  tty->parameters_present = 0;
  tty->private_csi = false;
  memset(tty->parameters, 0, sizeof(tty->parameters));
}

static void execute_escape(struct tty *tty, unsigned char byte)
{
  tty->escape_state = TTY_TEXT;
  if (byte == '[') {
    tty->escape_state = TTY_CSI_ENTRY;
  } else if (byte == '(' || byte == ')' || byte == '*' || byte == '+') {
    tty->escape_state = TTY_CHARSET;
  } else if (byte == '7') {
    save_cursor(tty);
  } else if (byte == '8') {
    restore_cursor(tty);
  } else if (byte == 'M') {
    reverse_index(tty);
  }
}

static void put_character(struct tty *tty, uint16_t character)
{
  /* Writing the margin must not scroll a full-screen application's last row.
   * Styles and cursor visibility preserve this pending wrap across writes. */
  if (tty->wrap_pending) {
    tty_newline(tty);
  }
  tty_draw_cell(tty, character, tty->x, tty->y);
  if (tty->x == tty->width - 1) {
    tty->wrap_pending = true;
  } else {
    ++tty->x;
  }
}

static void flush_utf8(struct tty *tty)
{
  size_t count = terminal_utf8_flush(&tty->utf8);
  for (size_t i = 0; i < count; ++i) {
    put_character(tty, TERMINAL_REPLACEMENT);
  }
}

static void put_char(struct tty *tty, char c)
{
  unsigned char byte = (unsigned char)c;
  if (byte < 0x20 || byte == 0x7f) {
    flush_utf8(tty);
  }
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
    execute_escape(tty, byte);
    return;
  }
  if (tty->escape_state == TTY_CHARSET) {
    /* Only the default set exists; the designation is consumed. */
    tty->escape_state = TTY_TEXT;
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
        tty->parameters_present |= (uint16_t)(1u << tty->parameter_index);
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

  uint16_t characters[4];
  size_t count = terminal_utf8_decode(&tty->utf8, byte, characters);
  for (size_t i = 0; i < count; ++i) {
    put_character(tty, characters[i]);
  }
}

void tty_put_char(struct tty *tty, char c)
{
  uint16_t x = tty->x;
  uint16_t y = tty->y;
  bool visible = tty->cursor_visible;
  uint32_t background = terminal_color_rgb(tty->style.background,
      tty->scheme->palette, tty->scheme->background);
  put_char(tty, c);
  if (visible != tty->cursor_visible ||
      (tty->cursor_visible && (x != tty->x || y != tty->y))) {
    ++tty->visual_generation;
  }
  if (background != terminal_color_rgb(tty->style.background,
      tty->scheme->palette, tty->scheme->background)) {
    ++tty->background_generation;
  }
}

size_t tty_storage_bytes(size_t width, size_t height)
{
  size_t cells;
  if (__builtin_mul_overflow(width, height, &cells) ||
      cells > (SIZE_MAX - height) / TTY_STORAGE_BYTES_PER_CELL) {
    return 0;
  }
  return cells * TTY_STORAGE_BYTES_PER_CELL + height;
}

void tty_attach_storage(struct tty *tty, uint8_t *storage)
{
  size_t cells = (size_t)tty->width * tty->height;
  tty->storage = storage;
  tty->cells = (struct terminal_cell *)storage;
  tty->other_cells = tty->cells + cells;
  tty->dirty = storage + cells * TTY_STORAGE_BYTES_PER_CELL;
  memset(tty->dirty, 0, tty->height);
  tty->origin = 0;
  tty->other_origin = 0;
  tty->alternate = false;
  redraw_all(tty);
  reset_region(tty);
  reset_saved_cursor(&tty->saved[0]);
  reset_saved_cursor(&tty->saved[1]);

  struct terminal_cell blank = styled_cell(tty, ' ', 0);
  for (size_t i = 0; i < cells; ++i) {
    tty->cells[i] = blank;
    tty->other_cells[i] = blank;
  }
}

void tty_clear(struct tty *tty)
{
  if (tty->cursor_visible && (tty->x || tty->y)) {
    ++tty->visual_generation;
  }
  blank_rows(tty, 0, tty->height);
  tty->x = 0;
  tty->y = 0;
  tty->escape_state = TTY_TEXT;
  tty->utf8 = (struct terminal_utf8){0};
  tty->wrap_pending = false;
}

void tty_fresh_line(struct tty *tty)
{
  uint16_t x = tty->x;
  uint16_t y = tty->y;
  flush_utf8(tty);
  tty->escape_state = TTY_TEXT;
  if (tty->x || tty->wrap_pending) {
    tty_newline(tty);
  }
  if (tty->cursor_visible && (x != tty->x || y != tty->y)) {
    ++tty->visual_generation;
  }
}

/* Copy a screen's cells into new storage, keeping the rows from first_row of
 * the old ring that starts at old_origin. */
static void copy_screen(struct terminal_cell *cells, size_t width, size_t height,
    const struct terminal_cell *old_cells, size_t old_width, size_t old_height,
    size_t old_origin, size_t first_row, size_t rows, size_t columns,
    struct terminal_cell blank)
{
  for (size_t i = 0; i < width * height; ++i) {
    cells[i] = blank;
  }
  for (size_t y = 0; y < rows; ++y) {
    size_t source = (old_origin + first_row + y) % old_height;
    memcpy(cells + y * width, old_cells + source * old_width, columns * sizeof(*cells));
  }
}

static size_t kept_first_row(size_t cursor_row, size_t height)
{
  return cursor_row >= height ? cursor_row - height + 1 : 0;
}

void tty_resize(struct tty *tty, const struct framebuffer *fb, uint8_t *storage)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  size_t width = fb->width / tty->font->width;
  size_t height = fb->height / tty->font->height;
  KASSERT(width && width <= UINT16_MAX && height && height <= UINT16_MAX);
  KASSERT(width <= SIZE_MAX / height);
  KASSERT(storage && tty->storage && storage != tty->storage);
  KASSERT(tty->geometry_generation && tty->geometry_generation < UINT64_MAX);

  size_t first_row = kept_first_row(tty->y, height);
  size_t rows = MIN((size_t)tty->height - first_row, height);
  size_t columns = MIN((size_t)tty->width, width);

  /* The hidden screen keeps the rows around its saved cursor. */
  size_t cells = width * height;
  struct terminal_cell *new_cells = (struct terminal_cell *)storage;
  struct terminal_cell *new_other_cells = new_cells + cells;
  struct terminal_cell blank = styled_cell(tty, ' ', 0);
  const struct tty_saved_cursor *hidden = &tty->saved[!tty->alternate];
  size_t other_first_row = kept_first_row(hidden->y, height);
  copy_screen(new_cells, width, height, tty->cells, tty->width, tty->height,
      tty->origin, first_row, rows, columns, blank);
  copy_screen(new_other_cells, width, height, tty->other_cells, tty->width, tty->height,
      tty->other_origin, other_first_row, MIN((size_t)tty->height - other_first_row, height),
      columns, blank);

  tty_selection_clear(tty);
  tty->fb = fb;
  tty->storage = storage;
  tty->cells = new_cells;
  tty->other_cells = new_other_cells;
  tty->origin = 0;
  tty->other_origin = 0;
  tty->dirty = storage + cells * TTY_STORAGE_BYTES_PER_CELL;
  memset(tty->dirty, 0, height);
  tty->width = width;
  tty->height = height;
  tty->x = MIN((size_t)tty->x, width - 1);
  tty->y = MIN((size_t)tty->y - first_row, height - 1);
  for (size_t screen = 0; screen < 2; ++screen) {
    struct tty_saved_cursor *saved = &tty->saved[screen];
    size_t kept = screen == tty->alternate ? first_row : other_first_row;
    saved->x = MIN((size_t)saved->x, width - 1);
    saved->y = MIN((size_t)saved->y - MIN((size_t)saved->y, kept), height - 1);
  }
  reset_region(tty);
  tty->wrap_pending = false;
  ++tty->geometry_generation;
  redraw_all(tty);
}

bool tty_lock(struct tty *tty)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  while (atomic_exchange_explicit(&tty->output_locked, true, memory_order_acquire)) {
    if (log_panic_started()) {
      return false;
    }
    __asm__ volatile("pause");
  }
  return true;
}

void tty_unlock(struct tty *tty)
{
  atomic_store_explicit(&tty->output_locked, false, memory_order_release);
}

bool tty_render_collect(struct tty *tty, struct tty_render *render)
{
  KASSERT(tty->height <= render->row_capacity &&
      (size_t)tty->width * tty->height <= render->cell_capacity);
  bool changed = tty->redraw || tty->scrolled;
  render->redraw = tty->redraw;
  render->scrolled = tty->scrolled;
  render->background = terminal_color_rgb(tty->style.background,
      tty->scheme->palette, tty->scheme->background);
  for (size_t y = 0; y < tty->height; ++y) {
    render->rows[y] = tty->redraw || tty->dirty[y];
    if (render->rows[y]) {
      memcpy(render->cells + y * tty->width, row_cells(tty, y),
          tty->width * sizeof(*tty->cells));
      changed = true;
    }
  }
  memset(tty->dirty, 0, tty->height);
  tty->scrolled = 0;
  tty->redraw = false;
  return changed;
}

void tty_render_draw(const struct tty *tty, const struct tty_render *render)
{
  const struct framebuffer *fb = tty->fb;
  uint8_t *pixels = (uint8_t *)fb->address;
  size_t row_bytes = fb->pitch * tty->font->height;
  if (render->redraw) {
    /* Margins right of and below the cell grid take the TTY background. */
    uint32_t background = framebuffer_color(fb, render->background);
    for (size_t y = 0; y < fb->height; ++y) {
      uint32_t *line = (uint32_t *)(pixels + y * fb->pitch);
      for (size_t x = 0; x < fb->width; ++x) {
        line[x] = background;
      }
    }
  } else if (render->scrolled > 0) {
    size_t rows = (size_t)render->scrolled;
    memmove(pixels, pixels + rows * row_bytes, (tty->height - rows) * row_bytes);
  } else if (render->scrolled < 0) {
    size_t rows = (size_t)-render->scrolled;
    memmove(pixels + rows * row_bytes, pixels, (tty->height - rows) * row_bytes);
  }
  for (size_t y = 0; y < tty->height; ++y) {
    if (!render->rows[y]) {
      continue;
    }
    const struct terminal_cell *cells = render->cells + y * tty->width;
    for (size_t x = 0; x < tty->width; ++x) {
      draw_cell(tty, cells[x], x, y);
    }
  }
}

void tty_selection_clear(struct tty *tty)
{
  if (tty->selection_valid) {
    ++tty->visual_generation;
  }
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
  bool valid = tty->selection_valid;
  size_t old_endpoint = tty->selection_endpoint;
  if (held && !same_cell) {
    tty->selection_valid = true;
  }
  if (tty->selection_valid) {
    tty->selection_endpoint = endpoint;
    if (!valid || old_endpoint != endpoint) {
      ++tty->visual_generation;
    }
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
