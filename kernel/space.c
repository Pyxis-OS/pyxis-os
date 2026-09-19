//
// Created by chronium on 9/18/26.
//

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

static struct framebuffer *fb_alloc(const struct boot_framebuffer *boot_fb)
{
  struct framebuffer *fb;
  fb = (struct framebuffer *)kmalloc(sizeof(struct framebuffer));

  fb->size = boot_fb->size;

  fb->width = boot_fb->width;
  fb->height = boot_fb->height;
  
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

static struct tty *tty_alloc(const struct boot_framebuffer *boot_fb) {
  struct tty *tty;
  tty = (struct tty *)kmalloc(sizeof(struct tty));

  tty->x = 0;
  tty->y = 0;

  tty->width = boot_fb->width / bizcat.width;
  tty->height = boot_fb->height / bizcat.height;

  tty->fg = aardvark_scheme.foreground;
  tty->bg = aardvark_scheme.background;

  tty->font = &bizcat;
  tty->scheme = &aardvark_scheme;
  tty->fb = boot_fb;

  //tty_clear();

  tty->initialized = true;

  return tty;
}

void space_init_all(const struct boot_framebuffer *boot_fb)
{
  char *name;
  struct space *space;

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

    space->fb = fb_alloc(boot_fb);
    space->tty = tty_alloc(boot_fb);

    arch_cpu_at(i)->space = space;
  }
}
