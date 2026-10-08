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
#include <kernel/mouse.h>
#include <kernel/object/console.h>
#include <kernel/object/display.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/pointer.h>
#include <kernel/display_capture.h>

#define PRESENT_INTERVAL_NS UINT64_C(16666667)

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
/* Registry index of the leftmost visible tab. Presenter-owned. */
static size_t viewport_first;
/* Whether the battery widget takes its slot. Presenter-owned, set per frame. */
static bool battery_shown;

static struct framebuffer *spaces_nav_fb;
/* One text row of the space area. The cursor's row is composed here, cursor
 * included, so the screen never shows that row without the cursor. */
static struct framebuffer *cursor_row_fb;

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

  tty->font = &bizcat;
  tty->scheme = &aardvark_scheme;
  tty->fb = fb;

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

  space->display = display_create(space);
  if (!space->display) {
    panic("cannot allocate space display");
  }

  space->keyboard = keyboard_create(space, focused);
  if (!space->keyboard) {
    panic("cannot allocate space keyboard");
  }

  space->pointer = pointer_create(space, focused);
  if (!space->pointer) {
    panic("cannot allocate space pointer");
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
};

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
  size_t minimum = (widest + 2 * SPACES_NAV_TITLE_PADDING_CELLS) * bizcat.width;
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

static void draw_tab(struct space *space, size_t x, size_t width)
{
  draw_layer_marker(space, x, width);
  fb_rect(spaces_nav_fb, x, 0, width, SPACES_NAV_HEIGHT, aardvark_scheme.palette[8]);

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

static void draw_spaces_nav()
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

  struct space *space = caelum_space;
  for (size_t i = 0; i < viewport_first; ++i) {
    space = space->next;
  }
  for (size_t i = 0; i < layout.visible; ++i) {
    draw_tab(space, chevron_width + i * layout.tab_width, layout.tab_width);
    space = space->next;
  }
}

/* Block cursor: the cell's background becomes the cursor color and its glyph
 * the cursor text color. Fonts are two-color bitmaps with blank padding, so
 * the top-left pixel is the cell's background. */
static void draw_block_cursor(struct framebuffer *row, size_t x, const struct tty *tty)
{
  uint32_t block = framebuffer_color(row, tty->scheme->cursor);
  uint32_t text = framebuffer_color(row, tty->scheme->cursor_text);
  uint32_t *origin = (uint32_t *)row->address + x;
  uint32_t background = origin[0];
  for (size_t line = 0; line < tty->font->height; ++line) {
    uint32_t *pixel = (uint32_t *)(row->address + line * row->pitch) + x;
    for (size_t i = 0; i < tty->font->width; ++i) {
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
  struct resize_space *next;
};

struct resize_buffers {
  struct resize_space *spaces;
  struct framebuffer *navigation;
  struct framebuffer *cursor;
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
    kfree(entry);
  }
  fb_free(buffers->navigation);
  fb_free(buffers->cursor);
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
  }
  buffers->navigation = fb_try_alloc(layout, layout->width, SPACES_NAV_HEIGHT);
  buffers->cursor = fb_try_alloc(layout, layout->width, bizcat.height);
  return buffers->navigation && buffers->cursor;
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
    tty_resize(entry->space->tty, entry->fb);
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
  display_resize_commit();
  screen = display_layout();
  uint64_t elapsed = arch_monotonic_ns() - started;
  log_end(locked);
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
static void present_graphics(const struct framebuffer *source, uint32_t background)
{
  size_t height = screen->height - SPACES_NAV_HEIGHT;
  if (source->width == screen->width && source->height == height &&
      source->pitch == screen->pitch) {
    screen_capture_copy(SPACES_NAV_HEIGHT * screen->pitch,
        (const void *)source->address, source->size);
    return;
  }
  size_t columns = MIN(source->width, screen->width);
  size_t row_bytes = columns * sizeof(uint32_t);
  fb_fill_rect(cursor_row_fb, 0, 0, cursor_row_fb->width, 1, background);
  for (size_t y = 0; y < height; ++y) {
    if (y < source->height && columns == screen->width &&
        source->pitch >= screen->pitch) {
      screen_capture_copy((SPACES_NAV_HEIGHT + y) * screen->pitch,
          (const void *)(source->address + y * source->pitch), screen->pitch);
      continue;
    }
    if (y < source->height) {
      memcpy((void *)cursor_row_fb->address,
          (const void *)(source->address + y * source->pitch), row_bytes);
    } else if (y == source->height) {
      fb_fill_rect(cursor_row_fb, 0, 0, cursor_row_fb->width, 1, background);
    }
    screen_capture_copy((SPACES_NAV_HEIGHT + y) * screen->pitch,
        (const void *)cursor_row_fb->address, screen->pitch);
  }
}

void space_present()
{
  if (!presenting && !begin_presenting()) {
    return;
  }
  const size_t dst_offset = SPACES_NAV_HEIGHT * screen->pitch;

  screen_capture_begin(screen, caelum_space->tty->geometry_generation);
  draw_spaces_nav();

  if (!display_begin_frame()) {
    screen_capture_finish(false);
    return;
  }
  screen_capture_copy(0, (const void *)spaces_nav_fb->address, spaces_nav_fb->size);

  struct space *space = active_space;
  uint64_t flags = cpu_save_interrupts();
  struct display_frame *frame = display_snapshot(space->display);
  cpu_restore_interrupts(flags);
  const struct framebuffer *source = frame ? &frame->fb : space->fb;

  /* Snapshot the cursor under the output lock; never keep it held while
   * copying a frame. The TTY pixels themselves stay cursor-free. */
  flags = cpu_save_interrupts();
  bool locked = log_begin();
  const struct tty *tty = space->tty;
  bool visible = !frame && locked && tty->cursor_visible &&
      tty->x < tty->width && tty->y < tty->height;
  size_t x = tty->x, y = tty->y;
  uint32_t background = tty->bg;
  log_end(locked);
  cpu_restore_interrupts(flags);

  const uint8_t *pixels = (const uint8_t *)source->address;
  if (frame) {
    present_graphics(source, background);
  } else if (!visible) {
    screen_capture_copy(dst_offset, pixels, source->size);
  } else {
    /* Every screen write carries final pixels: rows above and below the
     * cursor go straight across, and the cursor row goes via cursor_row_fb. */
    size_t row_start = y * tty->font->height * source->pitch;
    size_t row_bytes = tty->font->height * source->pitch;
    screen_capture_copy(dst_offset, pixels, row_start);
    memcpy((void *)cursor_row_fb->address, pixels + row_start, row_bytes);
    draw_block_cursor(cursor_row_fb, x * tty->font->width, tty);
    screen_capture_copy(dst_offset + row_start,
        (const void *)cursor_row_fb->address, row_bytes);
    screen_capture_copy(dst_offset + row_start + row_bytes, pixels + row_start + row_bytes,
        source->size - row_start - row_bytes);
  }

  bool presented = display_end_frame();
  if (frame) {
    flags = cpu_save_interrupts();
    display_frame_release(frame);
    cpu_restore_interrupts(flags);
  }
  screen_capture_finish(presented);
}

void space_display_changed(struct space *space, bool discard_input)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  bool terminal_layer = space->display->presented && !space->display->visible;
  keyboard_set_layer(space->keyboard, terminal_layer, discard_input);
  pointer_focus(space->pointer, space == active_space && !terminal_layer);
}

/* BSP only, preserves IF. Restores the selected space's chosen input layer. */
static void switch_space(struct space *next)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  if (next != active_space) {
    keyboard_focus(active_space->keyboard, false);
    pointer_focus(active_space->pointer, false);
    active_space = next;
    keyboard_focus(next->keyboard, true);
    bool terminal_layer = next->display->presented && !next->display->visible;
    pointer_focus(next->pointer, !terminal_layer);
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

static void handle_space_input(void)
{
  struct key_event event;
  static bool navigation_held[KEY_COUNT];
  const unsigned shortcut_modifiers =
      KEY_MOD_SHIFT | KEY_MOD_CONTROL | KEY_MOD_ALT | KEY_MOD_SUPER;

  while (keyboard_read_event(&event)) {
    if (event.action == KEY_STATE_RESET) {
      memset(navigation_held, 0, sizeof(navigation_held));
      uint64_t flags = cpu_save_interrupts();
      /* Lost scan bytes can include a space shortcut, so no queued stream can
       * be trusted to describe what the user meant to send. */
      for (struct space *space = caelum_space->next; space; space = space->next) {
        keyboard_reset_input(space->keyboard);
      }
      cpu_restore_interrupts(flags);
      continue;
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
    /* Caelum's space has no input reader. */
    if (active_space == caelum_space) {
      continue;
    }

    uint64_t flags = cpu_save_interrupts();
    keyboard_route_event(active_space->keyboard, &event);
    cpu_restore_interrupts(flags);
  }
}

static void handle_pointer_input(void)
{
  static uint32_t device_buttons;
  struct mouse_event event;

  while (mouse_read_event(&event)) {
    uint64_t flags = cpu_save_interrupts();
    if (event.reset) {
      /* The device was not reset, so a button may still be held. Treat all as
       * held: each must be released and pressed again before it counts. */
      device_buttons = MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT | MOUSE_BUTTON_MIDDLE;
      for (struct space *space = caelum_space->next; space; space = space->next) {
        pointer_reset_input(space->pointer);
      }
    } else {
      /* Presses are judged against the device, not a space, so a button held
       * across a space switch is not new to the space that gains focus. */
      uint32_t pressed = event.buttons & ~device_buttons;
      device_buttons = event.buttons;
      if (active_space != caelum_space) {
        pointer_route_event(active_space->pointer, &event, pressed);
      }
    }
    cpu_restore_interrupts(flags);
  }
}

void space_present_task(void *argument)
{
  (void)argument;
  bool available = display_start();
  uint64_t deadline = arch_monotonic_ns();

  for (;;) {
    handle_space_input();
    handle_pointer_input();
    if (available) {
      if (presenting || begin_presenting()) {
        resize_display();
      }
      space_present();
    } else {
      screen_capture_finish(false);
    }
    deadline += PRESENT_INTERVAL_NS;
    uint64_t now = arch_monotonic_ns();
    if (deadline <= now) {
      /* Drop missed frames rather than catching up in a busy loop. */
      deadline = now + PRESENT_INTERVAL_NS;
    }
    kernel_task_sleep_until(deadline);
  }
}
