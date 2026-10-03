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
#include <kernel/object/console.h>
#include <kernel/object/display.h>
#include <kernel/object/keyboard.h>

#define PRESENT_INTERVAL_NS UINT64_C(16666667)

#define SPACES_NAV_HEIGHT 32
#define SPACES_NAV_COUNT 4

static const struct boot_framebuffer *screen;
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

void space_init_all(const struct boot_framebuffer *boot_fb)
{
  screen = boot_fb;

  for (size_t i = 0; i < arch_cpu_count(); ++i) {
    arch_clock_maintain();
    struct space *space = kmalloc(sizeof(*space));
    if (!space) {
      panic("cannot allocate space");
    }
    *space = (struct space){0};
    atomic_init(&space->title_locked, false);
    if (i == 0) {
      static_assert(sizeof(KERNEL_NAME) <= sizeof(space->title));
      memcpy(space->title, KERNEL_NAME, sizeof(KERNEL_NAME));
    } else {
      sprintf(space->title, "CPU %zu", i);
    }
    klog("Initializing Space: %s\n", space->title);

    space->fb = fb_alloc(boot_fb, boot_fb->width, 
        boot_fb->height - SPACES_NAV_HEIGHT);
    space->tty = tty_alloc(space->fb);
    space->console = console_create(space->tty);
    if (!space->console) {
      panic("cannot allocate space console");
    }

    space->display = display_create(space);
    if (!space->display) {
      panic("cannot allocate space display");
    }

    space->keyboard = keyboard_create(space, i == 0);
    if (!space->keyboard) {
      panic("cannot allocate space keyboard");
    }

    arch_cpu_at(i)->space = space;

    if (i == 0) {
      log_set_tty(space->tty);
      active_space = space;
    }
  }

  spaces_nav_fb = fb_alloc(boot_fb, boot_fb->width, SPACES_NAV_HEIGHT);
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

  for (size_t i = 0; i < SPACES_NAV_COUNT; i++) {
    fb_fill_rect(spaces_nav_fb, tab_width * i, 0, tab_width, SPACES_NAV_HEIGHT,
        aardvark_scheme.palette[0]);
    fb_rect(spaces_nav_fb, tab_width * i, 0, tab_width, SPACES_NAV_HEIGHT,
        aardvark_scheme.palette[8]);

    if (i < arch_cpu_count()) {
      struct space *space = arch_cpu_at(i)->space;
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

static void switch_adjacent_space(bool next)
{
  size_t count = arch_cpu_count();
  for (size_t index = 0; index < count; ++index) {
    if (arch_cpu_at(index)->space != active_space) {
      continue;
    }

    if (next) {
      space_switch(index + 1 == count ? 0 : index + 1);
    } else {
      space_switch(index == 0 ? count - 1 : index - 1);
    }
    return;
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
      for (size_t i = arch_cpu_count() > 1 ? 1 : 0; i < arch_cpu_count(); ++i) {
        keyboard_reset_input(arch_cpu_at(i)->space->keyboard);
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
    if (arch_cpu_count() > 1 && active_space == arch_cpu_at(0)->space) {
      continue;
    }

    uint64_t flags = cpu_save_interrupts();
    keyboard_route_event(active_space->keyboard, &event);
    cpu_restore_interrupts(flags);
  }
}

void space_present_task(void *argument)
{
  (void)argument;
  uint64_t deadline = arch_monotonic_ns();

  for (;;) {
    handle_space_input();
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

void space_switch(size_t index)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  if (index < arch_cpu_count()) {
    struct space *next = arch_cpu_at(index)->space;
    if (next != active_space) {
      keyboard_focus(active_space->keyboard, false);
      active_space = next;
      keyboard_focus(next->keyboard, true);
    }
  }
  cpu_restore_interrupts(flags);
}
