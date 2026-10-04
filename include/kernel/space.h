//
// Created by chronium on 9/18/26.
//

#ifndef PYXIS_OS_SPACE_H
#define PYXIS_OS_SPACE_H

#include <abi/space.h>
#include <stdatomic.h>
#include <kernel/fb/fb.h>
#include <kernel/fb/tty.h>
#include <kernel/boot.h>

struct console_object;
struct display_object;
struct keyboard_object;
struct pointer_object;

struct space
{
  char title[SPACE_TITLE_MAX + 1]; /* Access under title_locked after boot. */
  atomic_bool title_locked;
  struct framebuffer *fb;
  struct tty *tty;
  struct keyboard_object *keyboard; /* Space retains the initial reference. */
  struct pointer_object *pointer; /* Space retains the initial reference. */
  struct display_object *display; /* Space retains the initial reference. */
  struct console_object *console; /* Space retains the initial reference. */
};

void space_init_all(const struct boot_framebuffer *boot_fb);

/* Copies a validated title without allocation. Preserves IF. */
bool space_set_title(struct space *space, const char *title, size_t length);

void space_present();
/* BSP kernel-task entry; argument is unused. */
void space_present_task(void *argument);
/* BSP only, preserves IF. Updates keyboard and pointer focus together with selection. */
void space_switch(size_t index);

#endif // PYXIS_OS_SPACE_H
