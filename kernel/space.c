#include <kernel/object/clipboard.h>
//
// Created by chronium on 9/18/26.
//

#include <arch/clock.h>
#include "arch/cpu.h"
#include "kernel/fb/font.h"
#include "kernel/fb/tty.h"
#include <kernel/fb/early_console.h>
#include <kernel/acpi.h>
#include <kernel/mm/types.h>
#include <kernel/space.h>
#include <kernel/display.h>
#include <kernel/defs.h>
#include <arch/smp.h>
#include <kernel/string.h>
#include <kernel/format.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/mm/types.h>
#include <kernel/panic.h>
#include <arch/cpu_local.h>
#include <kernel/memory.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/keyboard.h>
#include <kernel/input.h>
#include <kernel/mouse.h>
#include <kernel/object/console.h>
#include <kernel/object/display.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/pointer.h>
#include <kernel/object/audio.h>
#include <kernel/pointer.h>
#include <kernel/pointer_present.h>
#include <kernel/display_capture.h>
#include <kernel/volume_ui.h>
#include <kernel/ui/power_overlay.h>
#include "display/presentation.h"

#define PRESENT_INTERVAL_NS UINT64_C(16666667)
#define FLIP_POLL_INTERVAL_NS UINT64_C(1000000)

#define SPACES_NAV_HEIGHT 32
/* Character cells: a chevron slot at each end, and blank margin each side of a title. */
#define SPACES_NAV_CHEVRON_CELLS 2
#define SPACES_NAV_TITLE_PADDING_CELLS 1
/* The battery widget's slot, inside the right chevron: a box around three
 * characters, so its width never changes between 0% and 100%. */
#define BATTERY_WIDGET_CELLS 5
#define BATTERY_BOX_CELLS 4
#define BATTERY_TEXT_CELLS 3
#define BATTERY_BOX_PADDING 4

static const struct framebuffer *screen;
/* Registry order starts at Caelum. space_create appends on the BSP at any time. */
#define CAELUM_SPACE_NAME "caelum"
static struct space *caelum_space, *last_space;
static struct space *active_space;
static void handle_space_input(void);
/* Registry index of the leftmost visible tab. Presenter-owned. */
static size_t viewport_first;
/* Whether the battery widget takes its slot. Presenter-owned, set per frame. */
static bool battery_shown;

static struct framebuffer *spaces_nav_fb;
/* One text row of the space area. The cursor's row is composed here, cursor
 * included, so the screen never shows that row without the cursor. */
static struct framebuffer *cursor_row_fb;
static uint8_t *selection_row_glyphs;

static struct framebuffer *fb_try_alloc(const struct framebuffer *layout,
    size_t width, size_t height)
{
  if (!width || !height || width > SIZE_MAX / sizeof(uint32_t) ||
      layout->pitch < width * sizeof(uint32_t) || height > SIZE_MAX / layout->pitch) {
    return NULL;
  }
  size_t bytes = height * layout->pitch;
  if (bytes > SIZE_MAX - (PAGE_SIZE - 1)) {
    return NULL;
  }
  struct framebuffer *fb = kmalloc(sizeof(*fb));
  if (!fb) {
    return NULL;
  }
  *fb = (struct framebuffer){
    .width = width, .height = height, .pitch = layout->pitch, .size = bytes,
    .red_shift = layout->red_shift, .green_shift = layout->green_shift,
    .blue_shift = layout->blue_shift,
  };
  if (vm_alloc(vm_kernel_space(), bytes, PAGE_SIZE, PAGE_WRITE, &fb->address) != MM_OK) {
    kfree(fb);
    return NULL;
  }
  return fb;
}

static struct framebuffer *fb_alloc(const struct framebuffer *layout,
    size_t width, size_t height)
{
  struct framebuffer *fb = fb_try_alloc(layout, width, height);
  if (!fb) {
    panic("cannot allocate space framebuffer");
  }
  return fb;
}

static void fb_free(struct framebuffer *fb)
{
  if (fb) {
    KASSERT(vm_free(vm_kernel_space(), fb->address, fb->size) == MM_OK);
    kfree(fb);
  }
}

static uint8_t *tty_storage_try_alloc(const struct framebuffer *fb)
{
  size_t cells, bytes;
  if (__builtin_mul_overflow(fb->width / bizcat.width, fb->height / bizcat.height, &cells) ||
      !cells || __builtin_mul_overflow(cells, TTY_STORAGE_BYTES_PER_CELL, &bytes)) {
    return NULL;
  }
  return kmalloc(bytes);
}

static struct tty *tty_alloc(const struct framebuffer *fb) {
  struct tty *tty;
  tty = (struct tty *)kmalloc(sizeof(struct tty));
  if (!tty) {
    panic("cannot allocate space TTY");
  }
  *tty = (struct tty){0};

  tty->x = 0;
  tty->y = 0;

  tty->width = fb->width / bizcat.width;
  tty->height = fb->height / bizcat.height;
  tty->tab_width = TTY_DEFAULT_TAB_WIDTH;
  tty->geometry_generation = caelum_space ?
    caelum_space->tty->geometry_generation : 1;

  tty->fg = aardvark_scheme.foreground;
  tty->bg = aardvark_scheme.background;
  tty->foreground = TTY_DEFAULT_COLOR;
  tty->background = TTY_DEFAULT_COLOR;

  tty->font = &bizcat;
  tty->scheme = &aardvark_scheme;
  tty->fb = fb;

  uint8_t *storage = tty_storage_try_alloc(fb);
  if (!storage) {
    panic("cannot allocate space TTY cells");
  }
  tty_attach_storage(tty, storage);
  tty_clear(tty);

  tty->initialized = true;
  tty->cursor_visible = true;

  return tty;
}

struct space *space_caelum(void)
{
  return caelum_space;
}

size_t space_cpu_words(void)
{
  return (arch_cpu_count() + 63) / 64;
}

static bool cpu_bit(const uint64_t *cpus, size_t cpu_index)
{
  return cpu_index < arch_cpu_count() && (cpus[cpu_index / 64] >> (cpu_index % 64)) & 1;
}

bool space_ceiling_allows(const struct space *space, size_t cpu_index)
{
  return cpu_bit(space->ceiling_cpus, cpu_index);
}

bool space_allows_cpu(const struct space *space, size_t cpu_index)
{
  return cpu_bit(space->effective_cpus, cpu_index);
}

static uint64_t *cpu_set_copy(const uint64_t *source)
{
  size_t bytes = space_cpu_words() * sizeof(*source);
  uint64_t *copy = kmalloc(bytes);
  if (!copy) {
    panic("cannot allocate space CPU set");
  }
  memcpy(copy, source, bytes);
  return copy;
}

static struct space *space_alloc(const char *name, const char *title,
    uint64_t *ceiling_cpus, bool focused)
{
  KASSERT(arch_cpu_index() == 0 && ceiling_cpus);
  KASSERT(space_name_valid(name, strlen(name)));
  arch_clock_maintain();
  struct space *space = kmalloc(sizeof(*space));
  if (!space) {
    panic("cannot allocate space");
  }
  *space = (struct space){
    .ceiling_cpus = ceiling_cpus,
    .effective_cpus = cpu_set_copy(ceiling_cpus),
    .affinity_staging = cpu_set_copy(ceiling_cpus),
    .setup_open = true,
  };
  atomic_init(&space->title_locked, false);
  memcpy(space->name, name, strlen(name) + 1);
  size_t length = strlen(title);
  KASSERT(length && length <= SPACE_TITLE_MAX);
  memcpy(space->title, title, length + 1);
  ktrace("Initializing Space: %s\n", space->title);

  space->fb = fb_alloc(screen, screen->width, screen->height - SPACES_NAV_HEIGHT);
  space->tty = tty_alloc(space->fb);
  space->console = console_create(space->tty);
  if (!space->console) {
    panic("cannot allocate space console");
  }

  space->console->space = space;
  if (!clipboard_space_init(space)) {
    panic("cannot allocate space clipboard");
  }

  space->display = display_create(space);
  if (!space->display) {
    panic("cannot allocate space display");
  }

  space->keyboard = keyboard_create(space, focused);
  if (!space->keyboard) {
    panic("cannot allocate space keyboard");
  }

  space->terminal_pointer = terminal_pointer_create(space);
  if (!space->terminal_pointer) {
    panic("cannot allocate terminal pointer");
  }
  space->pointer = pointer_create(space);
  if (!space->pointer) {
    panic("cannot allocate space pointer");
  }
  space->audio = audio_create(space);
  if (!space->audio) {
    panic("cannot allocate space audio");
  }
  return space;
}

bool space_display_size_supported(size_t width, size_t height)
{
  size_t navigation_cells = 2 * SPACES_NAV_CHEVRON_CELLS + BATTERY_WIDGET_CELLS + 1;
  return width / bizcat.width >= navigation_cells &&
      width / bizcat.width <= UINT16_MAX &&
      height >= SPACES_NAV_HEIGHT + bizcat.height &&
      (height - SPACES_NAV_HEIGHT) / bizcat.height <= UINT16_MAX;
}

void space_init(void)
{
  screen = display_layout();
  if (!space_display_size_supported(screen->width, screen->height)) {
    panic("display too small for navigation and a terminal");
  }
  static_assert(sizeof(KERNEL_NAME) <= SPACE_TITLE_MAX + 1);
  /* Caelum's only user process is boot init, which runs on the BSP. */
  uint64_t *allowed = kmalloc(space_cpu_words() * sizeof(*allowed));
  if (!allowed) {
    panic("cannot allocate space CPU set");
  }
  memset(allowed, 0, space_cpu_words() * sizeof(*allowed));
  allowed[0] = 1;
  caelum_space = space_alloc(CAELUM_SPACE_NAME, KERNEL_NAME, allowed, true);
  caelum_space->console->serial = true;
  last_space = caelum_space;
  active_space = caelum_space;
  log_set_tty(caelum_space->tty);
  spaces_nav_fb = fb_alloc(screen, screen->width, SPACES_NAV_HEIGHT);
  cursor_row_fb = fb_alloc(screen, screen->width, bizcat.height);
  selection_row_glyphs = kmalloc(screen->width / bizcat.width);
  if (!selection_row_glyphs) {
    panic("cannot allocate selection row");
  }
  pointer_init();
}

bool space_name_valid(const char *name, size_t length)
{
  if (!length || length > SPACE_NAME_MAX) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    char c = name[i];
    if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-') {
      return false;
    }
  }
  return true;
}

bool space_name_taken(const char *name)
{
  KASSERT(arch_cpu_index() == 0);
  size_t length = strlen(name);
  for (struct space *space = caelum_space; space; space = space->next) {
    if (strlen(space->name) == length && !memcmp(space->name, name, length)) {
      return true;
    }
  }
  return false;
}

struct space *space_create(const char *name, const char *title, uint64_t *ceiling_cpus)
{
  KASSERT(!space_name_taken(name));
  struct space *space = space_alloc(name, title, ceiling_cpus, false);
  /* The presenter may be preempted mid-walk on this CPU; it only ever sees a
   * complete node at the tail. */
  __atomic_store_n(&last_space->next, space, __ATOMIC_RELEASE);
  last_space = space;
  return space;
}

void space_report(struct space *space, const char *text)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  bool locked = log_begin();
  while (*text) {
    tty_put_char(space->tty, *text++);
  }
  log_end(locked);
  cpu_restore_interrupts(flags);
}

void space_report_unstarted(struct space *space, const char *reason)
{
  KASSERT(arch_cpu_index() == 0);
  /* No task has run in the space, so nothing else reads its CPU sets yet. */
  memset(space->ceiling_cpus, 0, space_cpu_words() * sizeof(*space->ceiling_cpus));
  memset(space->effective_cpus, 0, space_cpu_words() * sizeof(*space->effective_cpus));
  /* Bounded: a 31-byte name and a reason of at most SPACE_REASON_MAX bytes. */
  char text[SPACE_NAME_MAX + SPACE_REASON_MAX + 32];
  KASSERT(strlen(reason) <= SPACE_REASON_MAX);
  sprintf(text, "space %s not started: %s\n", space->name, reason);
  klog("userspace: %s", text);
  space_report(space, text);
}

static void lock_title(struct space *space)
{
  while (atomic_exchange_explicit(&space->title_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_title(struct space *space)
{
  atomic_store_explicit(&space->title_locked, false, memory_order_release);
}

bool space_set_title(struct space *space, const char *title, size_t length)
{
  if (!length || length > SPACE_TITLE_MAX) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    if ((unsigned char)title[i] < 0x20 || (unsigned char)title[i] > 0x7e) {
      return false;
    }
  }

  uint64_t flags = cpu_save_interrupts();
  lock_title(space);
  memcpy(space->title, title, length);
  space->title[length] = '\0';
  unlock_title(space);
  cpu_restore_interrupts(flags);
  return true;
}

static void snapshot_title(struct space *space, char title[SPACE_TITLE_MAX + 1])
{
  /* An AP can rename while the BSP presents. Copy under a short lock, with
   * preemption disabled; drawing never borrows the mutable title buffer. */
  uint64_t flags = cpu_save_interrupts();
  lock_title(space);
  memcpy(title, space->title, SPACE_TITLE_MAX + 1);
  unlock_title(space);
  cpu_restore_interrupts(flags);
}

/* Equal-width tabs, as many whole tabs as fit between the chevron slots. */
struct nav_layout {
  size_t count;
  size_t visible;
  size_t tab_width;
  bool master_icon, space_icons;
};

static struct nav_layout drawn_nav_layout;
static size_t drawn_viewport_first, drawn_nav_width;
static bool drawn_nav_valid;

static struct nav_layout nav_layout(void)
{
  struct nav_layout layout = {0};
  size_t widest = 0;
  for (struct space *space = caelum_space; space; space = space->next) {
    char title[SPACE_TITLE_MAX + 1];
    snapshot_title(space, title);
    widest = MAX(widest, strlen(title));
    ++layout.count;
  }

  size_t chevrons = 2 * SPACES_NAV_CHEVRON_CELLS * bizcat.width;
  if (battery_shown) {
    chevrons += BATTERY_WIDGET_CELLS * bizcat.width;
  }
  size_t available = spaces_nav_fb->width > chevrons ? spaces_nav_fb->width - chevrons : 0;
  size_t control_minimum = (1 + 2 * SPACES_NAV_TITLE_PADDING_CELLS) * bizcat.width;
  layout.master_icon = available >= VOLUME_ICON_WIDTH + control_minimum;
  if (layout.master_icon) {
    available -= VOLUME_ICON_WIDTH;
  }
  layout.space_icons = available >= VOLUME_ICON_WIDTH + control_minimum;
  size_t minimum = (widest + 2 * SPACES_NAV_TITLE_PADDING_CELLS) * bizcat.width +
      (layout.space_icons ? VOLUME_ICON_WIDTH : 0);
  layout.visible = MIN(layout.count, MAX(available / minimum, 1));
  layout.tab_width = available / layout.visible;
  return layout;
}

static size_t space_index(const struct space *wanted)
{
  size_t index = 0;
  for (struct space *space = caelum_space; space != wanted; space = space->next) {
    ++index;
  }
  return index;
}

/* Keeps the selection inside a viewport that never extends past the list. */
static void keep_selection_visible(const struct nav_layout *layout, size_t selected)
{
  viewport_first = MIN(viewport_first, layout->count - layout->visible);
  if (selected < viewport_first) {
    viewport_first = selected;
  } else if (selected >= viewport_first + layout->visible) {
    viewport_first = selected - layout->visible + 1;
  }
}

/* After a move, also reveal the next space in that direction when there is room. */
static void reveal_neighbour(size_t selected, bool next)
{
  struct nav_layout layout = nav_layout();
  if (layout.visible > 1) {
    if (next && selected + 1 < layout.count &&
        selected + 1 >= viewport_first + layout.visible) {
      viewport_first = selected + 2 - layout.visible;
    } else if (!next && selected > 0 && selected - 1 < viewport_first) {
      viewport_first = selected - 1;
    }
  }
  keep_selection_visible(&layout, selected);
}

static void draw_ellipsis(size_t x, size_t y, uint32_t color)
{
  /* The font has no ellipsis glyph: three dots on the baseline of one cell. */
  size_t dot = bizcat.width / 4;
  size_t baseline = y + bizcat.height - 2 * dot - 1;
  for (size_t i = 0; i < 3; ++i) {
    fb_fill_rect(spaces_nav_fb, x + i * (dot + dot / 2), baseline, dot, dot, color);
  }
}

static void draw_chevron(size_t x, char glyph, bool more)
{
  size_t padding = (SPACES_NAV_HEIGHT - bizcat.height) / 2;
  size_t width = SPACES_NAV_CHEVRON_CELLS * bizcat.width;
  uint32_t color = more ? aardvark_scheme.foreground : aardvark_scheme.palette[8];
  tty_plot_char_raw(spaces_nav_fb, &bizcat, glyph, x + (width - bizcat.width) / 2, padding,
      color, aardvark_scheme.palette[0]);
}

static void draw_layer_marker(struct space *space, size_t x, size_t width)
{
  if (width < bizcat.width) {
    return;
  }

  uint64_t flags = cpu_save_interrupts();
  bool presented = space->display->presented;
  bool visible = space->display->visible;
  cpu_restore_interrupts(flags);
  if (!presented) {
    return;
  }

  size_t padding = (SPACES_NAV_HEIGHT - bizcat.height) / 2;
  size_t marker_x = x + width - bizcat.width;
  if (visible) {
    tty_plot_char_raw(spaces_nav_fb, &bizcat, '+', marker_x, padding,
        aardvark_scheme.foreground, aardvark_scheme.palette[0]);
  } else {
    /* Match the plus stroke's width, thickness and vertical position. */
    size_t stroke_width = 3 * bizcat.width / 4;
    size_t stroke_height = MAX(bizcat.height / 8, 1);
    fb_fill_rect(spaces_nav_fb, marker_x + (bizcat.width - stroke_width) / 2,
        padding + bizcat.height / 2, stroke_width, stroke_height,
        aardvark_scheme.foreground);
  }
}

static void draw_tab(struct space *space, size_t x, size_t width, bool volume_icon)
{
  fb_rect(spaces_nav_fb, x, 0, width, SPACES_NAV_HEIGHT, aardvark_scheme.palette[8]);
  if (volume_icon) {
    volume_ui_draw_icon(spaces_nav_fb, space, x + width - VOLUME_ICON_WIDTH,
        space != caelum_space);
    width -= VOLUME_ICON_WIDTH;
  }
  draw_layer_marker(space, x, width);

  char title[SPACE_TITLE_MAX + 1];
  snapshot_title(space, title);
  size_t margin = 2 * SPACES_NAV_TITLE_PADDING_CELLS * bizcat.width;
  size_t cells = width > margin ? (width - margin) / bizcat.width : 0;
  size_t length = strlen(title);
  bool clipped = length > cells;
  size_t shown = clipped ? (cells ? cells - 1 : 0) : length;
  size_t drawn = clipped ? cells : length;
  if (!drawn) {
    return;
  }

  size_t padding = (SPACES_NAV_HEIGHT - bizcat.height) / 2;
  size_t text_x = x + (width - drawn * bizcat.width) / 2;
  for (size_t i = 0; i < shown; ++i) {
    tty_plot_char_raw(spaces_nav_fb, &bizcat, title[i], text_x + i * bizcat.width,
        padding, aardvark_scheme.foreground, aardvark_scheme.palette[0]);
  }
  if (clipped) {
    draw_ellipsis(text_x + shown * bizcat.width, padding, aardvark_scheme.foreground);
  }
  if (space == active_space) {
    fb_fill_rect(spaces_nav_fb, text_x, padding + bizcat.height + 1,
        drawn * bizcat.width, 1, aardvark_scheme.foreground);
  }
}

struct gradient_stop {
  uint8_t percent;
  uint32_t color;
};

/* The widget's background follows the charge, from red when empty through
 * brown at 25% to green when full. */
static const struct gradient_stop battery_gradient[] = {
  {0, 0xaa0000},
  {25, 0x773300},
  {100, 0x669900},
};

static uint32_t blend_channel(uint32_t from, uint32_t to, unsigned shift, int step, int span)
{
  int a = (int)((from >> shift) & 0xff);
  int b = (int)((to >> shift) & 0xff);
  return (uint32_t)(a + (b - a) * step / span) << shift;
}

static uint32_t battery_color(uint8_t percent)
{
  size_t last = sizeof(battery_gradient) / sizeof(battery_gradient[0]) - 1;
  size_t i = 0;
  while (i + 1 < last && percent > battery_gradient[i + 1].percent) {
    ++i;
  }
  const struct gradient_stop *from = &battery_gradient[i], *to = &battery_gradient[i + 1];
  int step = percent - from->percent;
  int span = to->percent - from->percent;
  return blend_channel(from->color, to->color, 16, step, span) |
         blend_channel(from->color, to->color, 8, step, span) |
         blend_channel(from->color, to->color, 0, step, span);
}

/* "100" when full, otherwise two digits and a percent sign: "07%". */
static void battery_text(uint8_t percent, char text[BATTERY_TEXT_CELLS])
{
  if (percent >= 100) {
    text[0] = '1';
    text[1] = '0';
    text[2] = '0';
    return;
  }
  text[0] = (char)('0' + percent / 10);
  text[1] = (char)('0' + percent % 10);
  text[2] = '%';
}

static void draw_battery(size_t x, uint8_t percent)
{
  size_t padding = (SPACES_NAV_HEIGHT - bizcat.height) / 2;
  size_t box_x = x + (BATTERY_WIDGET_CELLS - BATTERY_BOX_CELLS) * bizcat.width / 2;
  uint32_t color = battery_color(percent);
  fb_fill_rect(spaces_nav_fb, box_x, padding - BATTERY_BOX_PADDING,
      BATTERY_BOX_CELLS * bizcat.width, bizcat.height + 2 * BATTERY_BOX_PADDING, color);

  char text[BATTERY_TEXT_CELLS];
  battery_text(percent, text);
  size_t text_x = box_x + (BATTERY_BOX_CELLS - BATTERY_TEXT_CELLS) * bizcat.width / 2;
  for (size_t i = 0; i < BATTERY_TEXT_CELLS; ++i) {
    tty_plot_char_raw(spaces_nav_fb, &bizcat, text[i], text_x + i * bizcat.width, padding,
        aardvark_scheme.foreground, color);
  }
}

static struct nav_layout draw_spaces_nav(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct acpi_battery_status battery = acpi_battery_status();
  cpu_restore_interrupts(flags);
  battery_shown = battery.present;

  struct nav_layout layout = nav_layout();
  /* A title change can shrink the visible count; keep the selection in view. */
  keep_selection_visible(&layout, space_index(active_space));

  fb_fill_rect(spaces_nav_fb, 0, 0, spaces_nav_fb->width, SPACES_NAV_HEIGHT,
      aardvark_scheme.palette[0]);
  size_t chevron_width = SPACES_NAV_CHEVRON_CELLS * bizcat.width;
  draw_chevron(0, '<', viewport_first > 0);
  draw_chevron(spaces_nav_fb->width - chevron_width, '>',
      viewport_first + layout.visible < layout.count);
  if (battery_shown) {
    draw_battery(spaces_nav_fb->width - chevron_width - BATTERY_WIDGET_CELLS * bizcat.width,
        battery.percent);
  }

  size_t master_x = spaces_nav_fb->width - chevron_width -
      (battery_shown ? BATTERY_WIDGET_CELLS * bizcat.width : 0) -
      (layout.master_icon ? VOLUME_ICON_WIDTH : 0);
  if (layout.master_icon) {
    volume_ui_draw_icon(spaces_nav_fb, NULL, master_x, true);
  }

  struct space *space = caelum_space;
  for (size_t i = 0; i < viewport_first; ++i) {
    space = space->next;
  }
  struct volume_ui_layout volume_layout = {.first_space = space, .tab_x = chevron_width,
    .tab_width = layout.tab_width, .tab_count = layout.visible, .master_x = master_x,
    .screen_width = screen->width, .screen_height = screen->height,
    .master_shown = layout.master_icon, .spaces_shown = layout.space_icons};
  volume_ui_begin_frame(&volume_layout, spaces_nav_fb);
  for (size_t i = 0; i < layout.visible; ++i) {
    draw_tab(space, chevron_width + i * layout.tab_width, layout.tab_width,
        layout.space_icons);
    space = space->next;
  }
  return layout;
}

/* Block cursor: the cell's background becomes the cursor color and its glyph
 * the cursor text color. Fonts are two-color bitmaps with blank padding, so
 * the top-left pixel is the cell's background. */
static void draw_block_cursor(struct framebuffer *row, size_t x,
    const struct font *font, const struct color_scheme *scheme)
{
  uint32_t block = framebuffer_color(row, scheme->cursor);
  uint32_t text = framebuffer_color(row, scheme->cursor_text);
  uint32_t *origin = (uint32_t *)row->address + x;
  uint32_t background = origin[0];
  for (size_t line = 0; line < font->height; ++line) {
    uint32_t *pixel = (uint32_t *)(row->address + line * row->pitch) + x;
    for (size_t i = 0; i < font->width; ++i) {
      pixel[i] = pixel[i] == background ? block : text;
    }
  }
}

/* Presenter-owned: set once the early console has handed over the screen. */
static bool presenting;

/* Take the screen from the early console before the first framebuffer write.
 * A false log_begin means a panic is in progress, so the lock is not held. */
static bool begin_presenting(void)
{
  uint64_t flags = cpu_save_interrupts();
  bool locked = log_begin();
  bool owned = locked && early_console_retire();
  log_end(locked);
  cpu_restore_interrupts(flags);
  if (owned) {
    presenting = true;
    klog("display: presentation started; early console retired\n");
  }
  return owned;
}

struct resize_space {
  struct space *space;
  struct framebuffer *fb;
  uint8_t *storage;
  struct resize_space *next;
};

struct resize_buffers {
  struct resize_space *spaces;
  struct framebuffer *navigation;
  struct framebuffer *cursor;
  uint8_t *glyphs;
};

/* A missing remote flush acknowledgement keeps one old batch mapped for the
 * boot. Further resizing stops before it can create another retained batch. */
static struct resize_buffers retained_resize;

static void resize_buffers_free(struct resize_buffers *buffers)
{
  while (buffers->spaces) {
    struct resize_space *entry = buffers->spaces;
    buffers->spaces = entry->next;
    fb_free(entry->fb);
    kfree(entry->storage);
    kfree(entry);
  }
  fb_free(buffers->navigation);
  fb_free(buffers->cursor);
  kfree(buffers->glyphs);
  *buffers = (struct resize_buffers){0};
}

/* BSP, IF=0: registry and geometry cannot change during this walk. Nothing
 * becomes visible to AP console writers until the complete set is ready. */
static bool resize_buffers_prepare(struct resize_buffers *buffers,
    const struct framebuffer *layout)
{
  struct resize_space **tail = &buffers->spaces;
  for (struct space *space = caelum_space; space; space = space->next) {
    if (space->tty->geometry_generation == UINT64_MAX) {
      return false;
    }
    struct resize_space *entry = kmalloc(sizeof(*entry));
    if (!entry) {
      return false;
    }
    *entry = (struct resize_space){.space = space};
    *tail = entry;
    tail = &entry->next;
    entry->fb = fb_try_alloc(layout, layout->width, layout->height - SPACES_NAV_HEIGHT);
    if (!entry->fb) {
      return false;
    }
    entry->storage = tty_storage_try_alloc(entry->fb);
    if (!entry->storage) {
      return false;
    }
  }
  buffers->navigation = fb_try_alloc(layout, layout->width, SPACES_NAV_HEIGHT);
  buffers->cursor = fb_try_alloc(layout, layout->width, bizcat.height);
  buffers->glyphs = kmalloc(layout->width / bizcat.width);
  return buffers->navigation && buffers->cursor && buffers->glyphs;
}

/* Sole presenter, between frame leases. Device waits retain the published
 * TTY set, including output that APs write while scanout is being changed. */
static void resize_display(void)
{
  const struct framebuffer *layout = display_resize_prepare();
  if (!layout) {
    return;
  }
  struct resize_buffers buffers = {0};
  uint64_t flags = cpu_save_interrupts();
  struct space *registry_tail = last_space;
  struct framebuffer previous = *screen;
  bool prepared = space_display_size_supported(layout->width, layout->height) &&
      resize_buffers_prepare(&buffers, layout);
  if (!prepared) {
    resize_buffers_free(&buffers);
  }
  cpu_restore_interrupts(flags);
  if (!prepared) {
    display_resize_cancel();
    klog("display: resize refused; cannot prepare all space buffers\n");
    return;
  }

  flags = cpu_save_interrupts();
  bool current = registry_tail == last_space && previous.address == screen->address &&
      previous.width == screen->width && previous.height == screen->height &&
      previous.pitch == screen->pitch;
  cpu_restore_interrupts(flags);
  if (!current) {
    display_resize_defer();
    flags = cpu_save_interrupts();
    resize_buffers_free(&buffers);
    cpu_restore_interrupts(flags);
    return;
  }
  if (!display_resize_switch()) {
    display_resize_cancel();
    flags = cpu_save_interrupts();
    resize_buffers_free(&buffers);
    cpu_restore_interrupts(flags);
    return;
  }

  flags = cpu_save_interrupts();
  current = registry_tail == last_space && previous.address == screen->address &&
      previous.width == screen->width && previous.height == screen->height &&
      previous.pitch == screen->pitch;
  if (!current) {
    cpu_restore_interrupts(flags);
    display_resize_defer();
    flags = cpu_save_interrupts();
    resize_buffers_free(&buffers);
    cpu_restore_interrupts(flags);
    return;
  }

  bool locked = log_begin();
  if (!locked) {
    cpu_restore_interrupts(flags);
    display_resize_cancel();
    flags = cpu_save_interrupts();
    resize_buffers_free(&buffers);
    cpu_restore_interrupts(flags);
    return;
  }
  uint64_t started = arch_monotonic_ns();
  size_t count = 0;
  for (struct resize_space *entry = buffers.spaces; entry; entry = entry->next) {
    struct framebuffer *old = entry->space->fb;
    uint8_t *old_storage = entry->space->tty->storage;
    tty_resize(entry->space->tty, entry->fb, entry->storage);
    entry->storage = old_storage;
    entry->space->fb = entry->fb;
    entry->fb = old;
    ++count;
  }
  struct framebuffer *old = spaces_nav_fb;
  spaces_nav_fb = buffers.navigation;
  buffers.navigation = old;
  old = cursor_row_fb;
  cursor_row_fb = buffers.cursor;
  buffers.cursor = old;
  uint8_t *old_glyphs = selection_row_glyphs;
  selection_row_glyphs = buffers.glyphs;
  buffers.glyphs = old_glyphs;
  display_resize_commit();
  screen = display_layout();
  uint64_t elapsed = arch_monotonic_ns() - started;
  log_end(locked);
  drawn_nav_valid = false;
  volume_ui_cancel();
  volume_ui_end_frame(false);
  for (struct space *space = caelum_space; space; space = space->next) {
    pointer_geometry_changed(space);
    pointer_terminal_geometry_changed(space);
  }
  readiness_notify();
  cpu_restore_interrupts(flags);

  /* The previous presentation finished before this transaction. Its old GPU
   * backing has separate device ownership; TTY backing needs remote TLB flushes
   * even though no writer can retain an old pixel pointer beyond the lock. */
  display_resize_finish();
  if (vm_kernel_flush_remote()) {
    flags = cpu_save_interrupts();
    resize_buffers_free(&buffers);
    cpu_restore_interrupts(flags);
  } else {
    KASSERT(!retained_resize.spaces);
    retained_resize = buffers;
    display_resize_disable();
    klog("display: remote TLB flush timed out; old buffers retained, resizing stopped\n");
  }
  klog("display: resized to %lux%lu; copied %lu TTYs under output lock in %lu ns\n",
      screen->width, screen->height, count, elapsed);
}

/* A session keeps its original extent and pitch. The destination alone changes;
 * fill exposed margins and copy the top-left intersection by each source row. */
static void present_graphics(const struct framebuffer *source, uint32_t background,
    const struct pointer_frame *pointer)
{
  size_t height = screen->height - SPACES_NAV_HEIGHT;
  if (source->width == screen->width && source->height == height &&
      source->pitch == screen->pitch) {
    volume_ui_present_copy(screen, pointer, SPACES_NAV_HEIGHT * screen->pitch,
        (const void *)source->address, source->size);
    return;
  }
  size_t columns = MIN(source->width, screen->width);
  size_t row_bytes = columns * sizeof(uint32_t);
  fb_fill_rect(cursor_row_fb, 0, 0, cursor_row_fb->width, 1, background);
  for (size_t y = 0; y < height; ++y) {
    if (y < source->height && columns == screen->width &&
        source->pitch >= screen->pitch) {
      volume_ui_present_copy(screen, pointer, (SPACES_NAV_HEIGHT + y) * screen->pitch,
          (const void *)(source->address + y * source->pitch), screen->pitch);
      continue;
    }
    if (y < source->height) {
      memcpy((void *)cursor_row_fb->address,
          (const void *)(source->address + y * source->pitch), row_bytes);
    } else if (y == source->height) {
      fb_fill_rect(cursor_row_fb, 0, 0, cursor_row_fb->width, 1, background);
    }
    volume_ui_present_copy(screen, pointer, (SPACES_NAV_HEIGHT + y) * screen->pitch,
        (const void *)cursor_row_fb->address, screen->pitch);
  }
}

/* Composed from the overlay alone: no space frame or TTY, no bar and no
 * output lock, so a wedged program or TTY writer cannot hold it back. */
static void present_power_overlay(void)
{
  uint64_t flags = cpu_save_interrupts();
  power_overlay_update();
  drawn_nav_valid = false;
  cpu_restore_interrupts(flags);
  screen_capture_begin(screen, caelum_space->tty->geometry_generation);
  if (!display_begin_frame()) {
    flags = cpu_save_interrupts();
    volume_ui_end_frame(false);
    cpu_restore_interrupts(flags);
    screen_capture_finish(false);
    return;
  }
  flags = cpu_save_interrupts();
  struct pointer_frame pointer;
  pointer_frame_snapshot(&pointer);
  cpu_restore_interrupts(flags);
  for (size_t top = 0; top < screen->height; top += cursor_row_fb->height) {
    power_overlay_draw_band(cursor_row_fb, top, screen->width, screen->height);
    size_t rows = MIN(cursor_row_fb->height, screen->height - top);
    pointer_present_copy(screen, &pointer, top * screen->pitch,
        (const void *)cursor_row_fb->address, rows * screen->pitch);
  }
  bool presented = display_end_frame(&pointer);
  while (display_frame_pending()) {
    presented = display_frame_poll();
    if (display_frame_pending()) {
      handle_space_input();
      space_pointer_sync_input();
      kernel_task_sleep_until(arch_monotonic_ns() + FLIP_POLL_INTERVAL_NS);
    }
  }
  flags = cpu_save_interrupts();
  volume_ui_end_frame(false);
  cpu_restore_interrupts(flags);
  screen_capture_finish(presented);
  flags = cpu_save_interrupts();
  pointer_frame_release(&pointer);
  cpu_restore_interrupts(flags);
}

void space_present()
{
  if (!presenting && !begin_presenting()) {
    return;
  }
  if (power_overlay_shown()) {
    present_power_overlay();
    return;
  }
  const size_t dst_offset = SPACES_NAV_HEIGHT * screen->pitch;

  screen_capture_begin(screen, caelum_space->tty->geometry_generation);
  struct nav_layout nav = draw_spaces_nav();
  size_t nav_first = viewport_first;
  size_t nav_width = spaces_nav_fb->width;

  if (!display_begin_frame()) {
    drawn_nav_valid = false;
    volume_ui_end_frame(false);
    screen_capture_finish(false);
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  struct space *space = active_space;
  struct display_frame *frame = display_snapshot(space->display);
  struct pointer_frame pointer_snapshot;
  pointer_frame_snapshot(&pointer_snapshot);
  const struct pointer_frame *pointer = &pointer_snapshot;
  cpu_restore_interrupts(flags);
  volume_ui_present_copy(screen, pointer, 0,
      (const void *)spaces_nav_fb->address, spaces_nav_fb->size);
  const struct framebuffer *source = frame ? &frame->fb : space->fb;

  uint64_t output_flags = cpu_save_interrupts();
  bool composed = true;
  bool output_locked = log_begin();
  if (!output_locked) {
    cpu_restore_interrupts(output_flags);
    composed = false;
    goto frame_done;
  }
  uint32_t background = space->tty->bg;
  const struct font *font = space->tty->font;
  const struct color_scheme *scheme = space->tty->scheme;
  size_t rows = space->tty->height;
  log_end(output_locked);
  cpu_restore_interrupts(output_flags);

  if (frame) {
    present_graphics(source, background, pointer);
  } else {
    const struct tty *tty = space->tty;
    const uint8_t *pixels = (const uint8_t *)source->address;
    size_t copied = 0;
    size_t row_bytes = font->height * source->pitch;
    for (size_t row = 0; row < rows; ++row) {
      flags = cpu_save_interrupts();
      bool locked = log_begin();
      if (!locked) {
        cpu_restore_interrupts(flags);
        composed = false;
        goto frame_done;
      }
      size_t first = 0, last = 0;
      bool selected = locked && !space->terminal_pointer->owner &&
          tty_selection_row(tty, row, &first, &last);
      bool caret = locked && tty->cursor_visible && tty->y == row && tty->x < tty->width;
      size_t x = tty->x;
      if (selected || caret) {
        memcpy((void *)cursor_row_fb->address, pixels + row * row_bytes, row_bytes);
        if (selected) {
          memcpy(selection_row_glyphs + first, tty->cells + row * tty->width + first,
              last - first + 1);
        }
      }
      log_end(locked);
      cpu_restore_interrupts(flags);
      if (!selected && !caret) {
        continue;
      }
      size_t row_start = row * row_bytes;
      volume_ui_present_copy(screen, pointer, dst_offset + copied, pixels + copied,
          row_start - copied);
      if (selected) {
        for (size_t column = first; column <= last; ++column) {
          tty_plot_char_raw(cursor_row_fb, font, selection_row_glyphs[column],
              column * font->width, 0, scheme->selection, scheme->selection_background);
        }
      }
      if (caret) {
        draw_block_cursor(cursor_row_fb, x * font->width, font, scheme);
      }
      volume_ui_present_copy(screen, pointer, dst_offset + row_start,
          (const void *)cursor_row_fb->address, row_bytes);
      copied = row_start + row_bytes;
    }
    volume_ui_present_copy(screen, pointer, dst_offset + copied, pixels + copied,
        source->size - copied);
  }

frame_done:
  bool presented = display_end_frame(composed ? pointer : NULL);
  while (display_frame_pending()) {
    presented = display_frame_poll();
    if (display_frame_pending()) {
      handle_space_input();
      space_pointer_sync_input();
      kernel_task_sleep_until(arch_monotonic_ns() + FLIP_POLL_INTERVAL_NS);
    }
  }
  presented = presented && composed;
  flags = cpu_save_interrupts();
  if (presented) {
    drawn_nav_layout = nav;
    drawn_viewport_first = nav_first;
    drawn_nav_width = nav_width;
  }
  drawn_nav_valid = presented;
  volume_ui_end_frame(presented);
  cpu_restore_interrupts(flags);
  if (frame) {
    flags = cpu_save_interrupts();
    display_frame_release(frame);
    cpu_restore_interrupts(flags);
  }
  screen_capture_finish(presented);
  flags = cpu_save_interrupts();
  pointer_frame_release(&pointer_snapshot);
  cpu_restore_interrupts(flags);
}

void space_display_changed(struct space *space, bool discard_input)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  bool terminal_layer = space->display->presented && !space->display->visible;
  keyboard_set_layer(space->keyboard, terminal_layer, discard_input);
  pointer_space_changed(space);
}

/* BSP only, preserves IF. Restores the selected space's chosen input layer. */
static void switch_space(struct space *next)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  if (next != active_space) {
    volume_ui_cancel();
    keyboard_focus(active_space->keyboard, false);
    struct space *previous = active_space;
    active_space = next;
    keyboard_focus(next->keyboard, true);
    pointer_space_changed(previous);
    pointer_space_changed(next);
  }
  cpu_restore_interrupts(flags);
}

/* Selection stops at both ends of the registry; it does not wrap. */
static void switch_adjacent_space(bool next)
{
  if (next) {
    if (active_space->next) {
      switch_space(active_space->next);
      reveal_neighbour(space_index(active_space), true);
    }
    return;
  }
  struct space *previous = NULL;
  for (struct space *space = caelum_space; space != active_space; space = space->next) {
    previous = space;
  }
  if (previous) {
    switch_space(previous);
    reveal_neighbour(space_index(active_space), false);
  }
}

static void handle_space_input_locked(void)
{
  struct key_event event;
  static bool navigation_held[KEY_COUNT];
  static bool volume_held[KEY_COUNT];
  static bool escape_held;
  static bool delete_held;
  static bool overlay_held[KEY_COUNT];
  const unsigned shortcut_modifiers =
      KEY_MOD_SHIFT | KEY_MOD_CONTROL | KEY_MOD_ALT | KEY_MOD_SUPER;

  while (keyboard_read_event(&event)) {
    if (event.action == KEY_STATE_RESET) {
      memset(navigation_held, 0, sizeof(navigation_held));
      memset(volume_held, 0, sizeof(volume_held));
      memset(overlay_held, 0, sizeof(overlay_held));
      escape_held = delete_held = false;
      uint64_t flags = cpu_save_interrupts();
      volume_ui_cancel();
      clipboard_key_event(active_space, &event);
      /* Lost scan bytes can include a space shortcut, so no queued stream can
       * be trusted to describe what the user meant to send. */
      for (struct space *space = caelum_space->next; space; space = space->next) {
        keyboard_reset_input(space->keyboard);
      }
      cpu_restore_interrupts(flags);
      continue;
    }
    bool volume_suppressed = volume_held[event.key];
    bool clipboard_release = false;
    if (event.action == KEY_RELEASE) {
      volume_held[event.key] = false;
      /* Physical releases retire clipboard/Enter guards even while an overlay
       * consumes content input. Releases cannot admit a clipboard action. */
      clipboard_release = clipboard_key_event(active_space, &event);
    }
    /* Ctrl+Alt+Delete opens the power overlay. Like Super+Escape, the press,
     * its repeats and its release are consumed even if the modifiers go first. */
    if (event.key == KEY_DELETE || event.key == KEY_KP_PERIOD) {
      if (delete_held) {
        if (event.action == KEY_RELEASE) {
          delete_held = false;
        }
        continue;
      }
      if (event.action == KEY_PRESS &&
          (event.modifiers & (KEY_MOD_CONTROL | KEY_MOD_ALT)) ==
            (KEY_MOD_CONTROL | KEY_MOD_ALT)) {
        delete_held = true;
        if (presenting) {
          uint64_t flags = cpu_save_interrupts();
          power_overlay_open(active_space);
          cpu_restore_interrupts(flags);
        }
        continue;
      }
    }
    /* The overlay takes every key; keys it saw pressed stay consumed until
     * their release, after it closes too. */
    if (power_overlay_shown() || overlay_held[event.key]) {
      if (event.action == KEY_RELEASE) {
        overlay_held[event.key] = navigation_held[event.key] = volume_held[event.key] = false;
        if (event.key == KEY_ESCAPE) {
          escape_held = false;
        }
      } else {
        overlay_held[event.key] = true;
      }
      if (power_overlay_shown()) {
        uint64_t flags = cpu_save_interrupts();
        power_overlay_keyboard_input(&event);
        cpu_restore_interrupts(flags);
      }
      continue;
    }
    if (event.key == KEY_ESCAPE) {
      if ((event.modifiers & KEY_MOD_SUPER) && event.action == KEY_PRESS) {
        escape_held = true;
        uint64_t flags = cpu_save_interrupts();
        pointer_escape();
        cpu_restore_interrupts(flags);
        continue;
      }
      if (escape_held) {
        if (event.action == KEY_RELEASE) {
          escape_held = false;
        }
        continue;
      }
    }
    if (event.key == KEY_LEFT || event.key == KEY_RIGHT ||
        event.key == KEY_UP || event.key == KEY_DOWN) {
      if (navigation_held[event.key]) {
        if (event.action == KEY_RELEASE) {
          navigation_held[event.key] = false;
        }
        continue;
      }
      if ((event.modifiers & shortcut_modifiers) == KEY_MOD_SUPER &&
          event.action == KEY_PRESS) {
        navigation_held[event.key] = true;
        if (event.key == KEY_LEFT || event.key == KEY_RIGHT) {
          switch_adjacent_space(event.key == KEY_RIGHT);
        } else {
          uint64_t flags = cpu_save_interrupts();
          display_select_layer(active_space->display, event.key == KEY_UP);
          cpu_restore_interrupts(flags);
        }
        continue;
      }
    }
    if (volume_suppressed) {
      if (event.action != KEY_RELEASE && volume_ui_keyboard_focused()) {
        uint64_t flags = cpu_save_interrupts();
        volume_ui_keyboard_input(&event);
        cpu_restore_interrupts(flags);
      }
      continue;
    }
    uint64_t volume_flags = cpu_save_interrupts();
    bool volume_consumed = volume_ui_keyboard_input(&event);
    cpu_restore_interrupts(volume_flags);
    if (volume_consumed) {
      volume_held[event.key] = event.action != KEY_RELEASE;
      continue;
    }
    uint64_t clipboard_flags = cpu_save_interrupts();
    bool clipboard_consumed = event.action == KEY_RELEASE ? clipboard_release :
        clipboard_key_event(active_space, &event);
    cpu_restore_interrupts(clipboard_flags);
    if (clipboard_consumed) {
      continue;
    }
    /* Caelum's space has no input reader. */
    if (active_space == caelum_space) {
      continue;
    }

    uint64_t flags = cpu_save_interrupts();
    keyboard_route_event(active_space->keyboard, &event);
    cpu_restore_interrupts(flags);
  }
}

static void handle_space_input(void)
{
  uint64_t flags = cpu_save_interrupts();
  handle_space_input_locked();
  cpu_restore_interrupts(flags);
}

bool space_keyboard_sync_input(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  bool device_complete = keyboard_sync_device();
  handle_space_input_locked();
  bool complete = device_complete && keyboard_input_complete();
  cpu_restore_interrupts(flags);
  return complete;
}

struct space *space_pointer_active(void)
{
  return active_space;
}

bool space_pointer_input_available(void)
{
  return input_pointer_available();
}

uint32_t space_pointer_suppressed_buttons(void)
{
  return input_pointer_suppressed_buttons();
}

size_t space_pointer_content_y(void)
{
  return SPACES_NAV_HEIGHT;
}

struct space *space_pointer_tab(int64_t x, int64_t y)
{
  if (!drawn_nav_valid || x < 0 || y < 0 || y >= SPACES_NAV_HEIGHT ||
      (uint64_t)x >= drawn_nav_width || !drawn_nav_layout.tab_width) {
    return NULL;
  }
  size_t first_x = SPACES_NAV_CHEVRON_CELLS * bizcat.width;
  if ((uint64_t)x < first_x) {
    return NULL;
  }
  size_t index = ((uint64_t)x - first_x) / drawn_nav_layout.tab_width;
  if (index >= drawn_nav_layout.visible) {
    return NULL;
  }
  struct space *space = caelum_space;
  for (size_t i = 0; i < drawn_viewport_first + index; ++i) {
    space = space->next;
  }
  return space;
}

void space_pointer_select(struct space *space)
{
  switch_space(space);
}

void space_pointer_sync_input(void)
{
  input_pointer_drain();
}

void space_present_task(void *argument)
{
  (void)argument;
  bool available = display_start();
  uint64_t deadline = arch_monotonic_ns();

  for (;;) {
    handle_space_input();
    space_pointer_sync_input();
    if (available) {
      /* A resize takes the output lock; it waits until the overlay closes. */
      if ((presenting || begin_presenting()) && !power_overlay_shown()) {
        resize_display();
      }
      space_present();
    } else {
      uint64_t flags = cpu_save_interrupts();
      volume_ui_cancel();
      volume_ui_end_frame(false);
      cpu_restore_interrupts(flags);
      screen_capture_finish(false);
    }
    deadline += PRESENT_INTERVAL_NS;
    uint64_t now = arch_monotonic_ns();
    if (deadline <= now) {
      /* A synchronized flip already paced this frame. If the software
       * deadline is due, continue without adding another full interval. */
      deadline = display_frame_flip_completed() ? now : now + PRESENT_INTERVAL_NS;
    }
    kernel_task_sleep_until(deadline);
  }
}
