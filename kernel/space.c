//
// Created by chronium on 9/18/26.
//

#include "arch/cpu.h"
#include "kernel/fb/font.h"
#include "kernel/fb/tty.h"
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

#define PRESENT_INTERVAL_TICKS 2

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

  tty->x = 0;
  tty->y = 0;

  tty->width = fb->width / bizcat.width;
  tty->height = fb->height / bizcat.height;

  tty->fg = aardvark_scheme.foreground;
  tty->bg = aardvark_scheme.background;

  tty->font = &bizcat;
  tty->scheme = &aardvark_scheme;
  tty->fb = fb;

  tty_clear(tty);

  tty->initialized = true;

  return tty;
}

void space_init_all(const struct boot_framebuffer *boot_fb)
{
  char *name;
  struct space *space;

  screen = boot_fb;

  for (size_t i = 0; i < arch_cpu_count(); ++i) {
    if (i == 0) {
      name = strndup(KERNEL_NAME, strlen(KERNEL_NAME));
    } else {
      char buf[256];
      sprintf(buf, "CPU %ld", i);
      name = strndup(buf, strlen(buf));
    }

    klog("Initializing Space: %s\n", name);

    space = (struct space *)kmalloc(sizeof(struct space));

    space->name = name;

    space->fb = fb_alloc(boot_fb, boot_fb->width, 
        boot_fb->height - SPACES_NAV_HEIGHT);
    space->tty = tty_alloc(space->fb);

    arch_cpu_at(i)->space = space;

    if (i == 0) {
      log_set_tty(space->tty);
      active_space = space;
    }
  }

  spaces_nav_fb = fb_alloc(boot_fb, boot_fb->width, SPACES_NAV_HEIGHT);
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
      const struct space *space = arch_cpu_at(i)->space;

      size_t padding = bizcat.height / 2;
      size_t len = MIN(strlen(space->name), max_len);

      for (size_t j = 0; j < len; j++) {
        tty_plot_char_raw(spaces_nav_fb, &bizcat, space->name[j],
            tab_width * i + padding + j * bizcat.width, padding,
            aardvark_scheme.foreground, aardvark_scheme.palette[0]);
      }
    }
  }
}

void space_present()
{
  const size_t dst_offset = SPACES_NAV_HEIGHT * screen->pitch;

  draw_spaces_nav();

  // Copy Spaces nav framebuffer
  memcpy((void *)screen->address,
      (const void *)spaces_nav_fb->address,
      spaces_nav_fb->size);

  // Copy active space framebuffer
  memcpy((void *)(screen->address + dst_offset),
      (const void *)(active_space->fb->address),
      active_space->fb->size);

  cpu_store_fence();
}

void space_present_task(void *argument)
{
  (void)argument;

  for (;;) {
    space_present();
    kernel_task_sleep(PRESENT_INTERVAL_TICKS);
  }
}

void space_switch(size_t index)
{
  if (index < arch_cpu_count()) {
    active_space = arch_cpu_at(index)->space;
  }
}
