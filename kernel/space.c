//
// Created by chronium on 9/18/26.
//

#include <arch/clock.h>
#include "arch/cpu.h"
#include "kernel/fb/font.h"
#include "kernel/fb/tty.h"
#include <kernel/fb/early_console.h>
#include <kernel/mm/types.h>
#include <kernel/space.h>
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
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/object/console.h>
#include <kernel/object/display.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/pointer.h>

#define PRESENT_INTERVAL_NS UINT64_C(16666667)

#define SPACES_NAV_HEIGHT 32
#define SPACES_NAV_COUNT 4

static const struct boot_framebuffer *screen;
/* Registry order starts at Caelum. Boot appends before the presenter starts. */
static struct space *caelum_space, *last_space;
static struct space *active_space;

static struct framebuffer *spaces_nav_fb;

static struct framebuffer *fb_alloc(const struct boot_framebuffer *boot_fb,
    size_t width, size_t height)
{
  struct framebuffer *fb;
  fb = (struct framebuffer *)kmalloc(sizeof(struct framebuffer));

  fb->pitch = boot_fb->pitch;

  fb->width = width;
  fb->height = height;

  fb->size = fb->height * fb->pitch;
  
  fb->red_shift = boot_fb->red_shift;
  fb->green_shift = boot_fb->green_shift;
  fb->blue_shift = boot_fb->blue_shift;

  enum mm_result status = vm_alloc(vm_kernel_space(), 
      fb->size, PAGE_SIZE, PAGE_WRITE, &fb->address);

  if (status != MM_OK) {
    panic("cannot allocate space framebuffer (error %d)", (uint32_t)status);
  }

  return fb;
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

static struct space *space_alloc(const char *title, size_t cpu_index, bool focused)
{
  KASSERT(arch_cpu_index() == 0 && cpu_index < arch_cpu_count());
  arch_clock_maintain();
  struct space *space = kmalloc(sizeof(*space));
  if (!space) {
    panic("cannot allocate space");
  }
  *space = (struct space){.cpu_index = cpu_index};
  atomic_init(&space->title_locked, false);
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

void space_init(const struct boot_framebuffer *boot_fb)
{
  screen = boot_fb;
  static_assert(sizeof(KERNEL_NAME) <= SPACE_TITLE_MAX + 1);
  caelum_space = space_alloc(KERNEL_NAME, 0, true);
  last_space = caelum_space;
  active_space = caelum_space;
  log_set_tty(caelum_space->tty);
  spaces_nav_fb = fb_alloc(boot_fb, boot_fb->width, SPACES_NAV_HEIGHT);
}

struct space *space_create(const char *name, size_t cpu_index)
{
  struct space *space = space_alloc(name, cpu_index, false);
  last_space->next = space;
  last_space = space;
  return space;
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

static void draw_spaces_nav()
{
  const size_t tab_width = spaces_nav_fb->width / SPACES_NAV_COUNT;
  const size_t max_len = (tab_width / bizcat.width) - 2;

  struct space *space = caelum_space;
  for (size_t i = 0; i < SPACES_NAV_COUNT; i++) {
    fb_fill_rect(spaces_nav_fb, tab_width * i, 0, tab_width, SPACES_NAV_HEIGHT,
        aardvark_scheme.palette[0]);
    fb_rect(spaces_nav_fb, tab_width * i, 0, tab_width, SPACES_NAV_HEIGHT,
        aardvark_scheme.palette[8]);

    if (space) {
      char title[SPACE_TITLE_MAX + 1];
      snapshot_title(space, title);

      size_t padding = bizcat.height / 2;
      size_t len = MIN(strlen(title), max_len);

      for (size_t j = 0; j < len; j++) {
        tty_plot_char_raw(spaces_nav_fb, &bizcat, title[j],
            tab_width * i + padding + j * bizcat.width, padding,
            aardvark_scheme.foreground, aardvark_scheme.palette[0]);
      }

      if (space == active_space) {
        fb_fill_rect(spaces_nav_fb, tab_width * i + padding,
            padding + bizcat.height + 1, len * bizcat.width, 1,
            aardvark_scheme.foreground);
      }
      space = space->next;
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

void space_present()
{
  if (!presenting && !begin_presenting()) {
    return;
  }
  const size_t dst_offset = SPACES_NAV_HEIGHT * screen->pitch;

  draw_spaces_nav();

  // Copy Spaces nav framebuffer
  memcpy((void *)screen->address,
      (const void *)spaces_nav_fb->address,
      spaces_nav_fb->size);

  struct space *space = active_space;
  uint64_t flags = cpu_save_interrupts();
  struct display_frame *frame = display_snapshot(space->display);
  cpu_restore_interrupts(flags);
  const struct framebuffer *source = frame ? &frame->fb : space->fb;

  // Copy active space framebuffer
  memcpy((void *)(screen->address + dst_offset),
      (const void *)source->address, source->size);

  /* Composite the cursor onto the display, leaving the TTY pixels intact.
   * Snapshot under the output lock; never keep it held while copying a frame. */
  flags = cpu_save_interrupts();
  bool locked = log_begin();
  const struct tty *tty = space->tty;
  bool visible = !frame && locked && tty->cursor_visible;
  size_t x = tty->x, y = tty->y;
  log_end(locked);
  cpu_restore_interrupts(flags);
  if (visible) {
    struct framebuffer display = *space->fb;
    display.address = screen->address + dst_offset;
    fb_fill_rect(&display, x * tty->font->width,
        (y + 1) * tty->font->height - 1, tty->font->width, 1,
        tty->scheme->cursor);
  }

  cpu_store_fence();
  if (frame) {
    flags = cpu_save_interrupts();
    display_frame_release(frame);
    cpu_restore_interrupts(flags);
  }
}

/* BSP only, preserves IF. Moves keyboard and pointer focus with the selection. */
static void switch_space(struct space *next)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  if (next != active_space) {
    keyboard_focus(active_space->keyboard, false);
    pointer_focus(active_space->pointer, false);
    active_space = next;
    keyboard_focus(next->keyboard, true);
    pointer_focus(next->pointer, true);
  }
  cpu_restore_interrupts(flags);
}

/* Selection stops at both ends of the registry; it does not wrap. */
static void switch_adjacent_space(bool next)
{
  if (next) {
    if (active_space->next) {
      switch_space(active_space->next);
    }
    return;
  }
  struct space *previous = NULL;
  for (struct space *space = caelum_space; space != active_space; space = space->next) {
    previous = space;
  }
  if (previous) {
    switch_space(previous);
  }
}

static void handle_space_input(void)
{
  struct key_event event;
  static bool navigation_held[2];
  const unsigned shortcut_modifiers =
      KEY_MOD_SHIFT | KEY_MOD_CONTROL | KEY_MOD_ALT | KEY_MOD_SUPER;

  while (keyboard_read_event(&event)) {
    if (event.action == KEY_STATE_RESET) {
      navigation_held[0] = navigation_held[1] = false;
      uint64_t flags = cpu_save_interrupts();
      /* Lost scan bytes can include a space shortcut, so no queued stream can
       * be trusted to describe what the user meant to send. */
      for (struct space *space = caelum_space->next; space; space = space->next) {
        keyboard_reset_input(space->keyboard);
      }
      cpu_restore_interrupts(flags);
      continue;
    }
    if (event.key == KEY_LEFT || event.key == KEY_RIGHT) {
      size_t arrow = event.key == KEY_RIGHT;
      if (navigation_held[arrow]) {
        if (event.action == KEY_RELEASE) {
          navigation_held[arrow] = false;
        }
        continue;
      }
      if ((event.modifiers & shortcut_modifiers) == KEY_MOD_SUPER &&
          event.action == KEY_PRESS) {
        navigation_held[arrow] = true;
        switch_adjacent_space(arrow);
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
  uint64_t deadline = arch_monotonic_ns();

  for (;;) {
    handle_space_input();
    handle_pointer_input();
    space_present();
    deadline += PRESENT_INTERVAL_NS;
    uint64_t now = arch_monotonic_ns();
    if (deadline <= now) {
      /* Drop missed frames rather than catching up in a busy loop. */
      deadline = now + PRESENT_INTERVAL_NS;
    }
    kernel_task_sleep_until(deadline);
  }
}
