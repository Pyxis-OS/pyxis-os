#include <arch/cpu.h>
#include <kernel/fb/early_console.h>
#include <kernel/fb/font.h>
#include <stdatomic.h>
#include <stddef.h>

/* Bounds the BSS text grid; larger screens use a top-left region. */
#define EARLY_CONSOLE_COLUMNS_MAX 256
#define EARLY_CONSOLE_ROWS_MAX 128
/* Double the glyph size on high-resolution panels. */
#define EARLY_CONSOLE_DOUBLE_WIDTH 2560
#define EARLY_CONSOLE_TAB_WIDTH 8
#define EARLY_CONSOLE_FOREGROUND 0xc8ccd4
#define EARLY_CONSOLE_BACKGROUND 0x101216
/* No clock is assumed. Ample for another CPU to finish one glyph and notice
 * the panic. Expiry leaves the panic serial-only. */
#define EARLY_CONSOLE_DRAW_WAIT_LIMIT 1000000
#define EARLY_CONSOLE_NO_OWNER UINT32_MAX

enum early_console_state {
  EARLY_CONSOLE_OFF,
  EARLY_CONSOLE_ACTIVE,
  EARLY_CONSOLE_PANIC, /* Terminal: only panic_owner renders. */
  EARLY_CONSOLE_RETIRED, /* Terminal: the presenter owns the screen. */
};

static _Atomic enum early_console_state state = EARLY_CONSOLE_OFF;
/* APIC ID of the CPU inside an ordinary render step. The renderer stores it
 * before checking state and the panic claimant stores PANIC before reading
 * it, so one always observes the other. Cleared after the store fence. */
static _Atomic uint32_t drawing_cpu = EARLY_CONSOLE_NO_OWNER;
static _Atomic uint32_t panic_owner = EARLY_CONSOLE_NO_OWNER;
/* Owner-only: set while the panic owner draws, to recognize a fault raised by
 * drawing itself. */
static volatile bool panic_rendering;

/* Rendering state, changed only by the current rendering owner. */
static uintptr_t address;
static size_t pitch, scale, columns, rows, column, row;
static uint32_t foreground, background;
static char cells[EARLY_CONSOLE_ROWS_MAX][EARLY_CONSOLE_COLUMNS_MAX];

static uint32_t pack_color(const struct boot_framebuffer *fb, uint32_t rgb)
{
  return ((rgb >> 16 & 0xff) << fb->red_shift) | ((rgb >> 8 & 0xff) << fb->green_shift) |
      ((rgb & 0xff) << fb->blue_shift);
}

/* Owners keep rendering; an ordinary renderer stops once a panic or the
 * presenter has taken the screen. */
static bool may_render(bool owner)
{
  return owner || atomic_load(&state) == EARLY_CONSOLE_ACTIVE;
}

static void draw_cell(size_t cell_row, size_t cell_column)
{
  const struct font *font = &bizcat;
  unsigned char character = (unsigned char)cells[cell_row][cell_column];
  if (character > font->max_glyph) {
    character = '?';
  }
  const uint8_t *glyph = font->data + (size_t)character * font->stride;
  size_t x = cell_column * font->width * scale;
  size_t y = cell_row * font->height * scale;
  for (size_t glyph_row = 0; glyph_row < font->height; ++glyph_row) {
    for (size_t repeat = 0; repeat < scale; ++repeat) {
      volatile uint32_t *line = (volatile uint32_t *)(address +
          (y + glyph_row * scale + repeat) * pitch);
      for (size_t glyph_column = 0; glyph_column < font->width; ++glyph_column) {
        uint32_t color = (glyph[glyph_row] >> (glyph_column % 8)) & 1 ?
            foreground : background;
        for (size_t step = 0; step < scale; ++step) {
          line[x + glyph_column * scale + step] = color;
        }
      }
    }
  }
}

static void set_cell(size_t cell_row, size_t cell_column, char character)
{
  if (cells[cell_row][cell_column] != character) {
    cells[cell_row][cell_column] = character;
    draw_cell(cell_row, cell_column);
  }
}

/* Redraws only cells whose text changes, so blank regions cost nothing. */
static bool scroll(bool owner)
{
  for (size_t cell_row = 0; cell_row + 1 < rows; ++cell_row) {
    for (size_t cell_column = 0; cell_column < columns; ++cell_column) {
      if (!may_render(owner)) {
        return false;
      }
      set_cell(cell_row, cell_column, cells[cell_row + 1][cell_column]);
    }
  }
  for (size_t cell_column = 0; cell_column < columns; ++cell_column) {
    if (!may_render(owner)) {
      return false;
    }
    set_cell(rows - 1, cell_column, ' ');
  }
  return true;
}

static bool newline(bool owner)
{
  column = 0;
  if (row + 1 < rows) {
    ++row;
    return true;
  }
  return scroll(owner);
}

static void render(char character, bool owner)
{
  if (character == '\n') {
    newline(owner);
    return;
  }
  if (character == '\r') {
    column = 0;
    return;
  }
  size_t count = 1;
  if (character == '\t') {
    character = ' ';
    count = EARLY_CONSOLE_TAB_WIDTH - column % EARLY_CONSOLE_TAB_WIDTH;
  } else if (character < ' ' || character > '~') {
    character = '?';
  }
  for (size_t i = 0; i < count; ++i) {
    if (column == columns && !newline(owner)) {
      return;
    }
    if (!may_render(owner)) {
      return;
    }
    set_cell(row, column++, character);
  }
}

void early_console_start(const struct boot_framebuffer *fb, uintptr_t mapped)
{
  const struct font *font = &bizcat;
  scale = fb->width >= EARLY_CONSOLE_DOUBLE_WIDTH ? 2 : 1;
  columns = fb->width / (font->width * scale);
  rows = fb->height / (font->height * scale);
  if (columns > EARLY_CONSOLE_COLUMNS_MAX) {
    columns = EARLY_CONSOLE_COLUMNS_MAX;
  }
  if (rows > EARLY_CONSOLE_ROWS_MAX) {
    rows = EARLY_CONSOLE_ROWS_MAX;
  }
  if (!columns || !rows) {
    return;
  }
  address = mapped;
  pitch = fb->pitch;
  foreground = pack_color(fb, EARLY_CONSOLE_FOREGROUND);
  background = pack_color(fb, EARLY_CONSOLE_BACKGROUND);
  for (size_t y = 0; y < fb->height; ++y) {
    volatile uint32_t *line = (volatile uint32_t *)(address + y * pitch);
    for (size_t x = 0; x < fb->width; ++x) {
      line[x] = background;
    }
  }
  for (size_t cell_row = 0; cell_row < rows; ++cell_row) {
    for (size_t cell_column = 0; cell_column < columns; ++cell_column) {
      cells[cell_row][cell_column] = ' ';
    }
  }
  cpu_store_fence();
  atomic_store(&state, EARLY_CONSOLE_ACTIVE);
}

void early_console_rebind(uintptr_t mapped)
{
  address = mapped;
}

void early_console_putc(char character)
{
  if (atomic_load(&state) != EARLY_CONSOLE_ACTIVE) {
    return;
  }
  atomic_store(&drawing_cpu, cpu_initial_apic_id());
  if (atomic_load(&state) == EARLY_CONSOLE_ACTIVE) {
    render(character, false);
  }
  /* Drain write-combining stores before a panic owner may start drawing. */
  cpu_store_fence();
  atomic_store(&drawing_cpu, EARLY_CONSOLE_NO_OWNER);
}

void early_console_panic_begin(void)
{
  uint32_t self = cpu_initial_apic_id();
  enum early_console_state expected = EARLY_CONSOLE_ACTIVE;
  if (!atomic_compare_exchange_strong(&state, &expected, EARLY_CONSOLE_PANIC)) {
    /* The owner re-enters through panic() after an exception report. A fault
     * raised while it was drawing may come from the mapping itself, so stop
     * drawing rather than recurse. Other CPUs never owned the console. */
    if (atomic_load(&panic_owner) == self && panic_rendering) {
      atomic_store(&panic_owner, EARLY_CONSOLE_NO_OWNER);
    }
    return;
  }
  /* A render step interrupted on this CPU never resumes, so take over at once.
   * A step on another CPU may only be delayed: require its completion, and
   * stay serial-only rather than share the state if it does not finish. */
  uint32_t renderer = atomic_load(&drawing_cpu);
  if (renderer != self) {
    unsigned polls = 0;
    while (renderer != EARLY_CONSOLE_NO_OWNER) {
      if (++polls > EARLY_CONSOLE_DRAW_WAIT_LIMIT) {
        return;
      }
      __asm__ volatile("pause");
      renderer = atomic_load(&drawing_cpu);
    }
  }
  /* An interrupted renderer may have left a cursor update half done. */
  if (row >= rows) {
    row = rows - 1;
  }
  if (column > columns) {
    column = columns;
  }
  atomic_store(&panic_owner, self);
}

void early_console_panic_putc(char character)
{
  if (atomic_load(&panic_owner) == cpu_initial_apic_id()) {
    panic_rendering = true;
    render(character, true);
    cpu_store_fence();
    panic_rendering = false;
  }
}

bool early_console_retire(void)
{
  enum early_console_state expected = EARLY_CONSOLE_ACTIVE;
  if (atomic_compare_exchange_strong(&state, &expected, EARLY_CONSOLE_RETIRED)) {
    return true;
  }
  if (expected == EARLY_CONSOLE_OFF) {
    expected = EARLY_CONSOLE_OFF;
    if (atomic_compare_exchange_strong(&state, &expected, EARLY_CONSOLE_RETIRED)) {
      return true;
    }
  }
  return expected == EARLY_CONSOLE_RETIRED;
}
