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
  /* Every task in this space runs on this CPU until task migration exists. */
  size_t cpu_index;
  struct space *next; /* Registry order; fixed once the scheduler starts. */
};

/* Creates Caelum's space, first in registry order, on CPU 0. BSP only, at boot. */
void space_init(const struct boot_framebuffer *boot_fb);
/* Appends a workload space titled NAME whose tasks run on CPU_INDEX. BSP only,
 * before task_schedule(); spaces are never destroyed. Panics on exhaustion. */
struct space *space_create(const char *name, size_t cpu_index);

/* Copies a validated title without allocation. Preserves IF. */
bool space_set_title(struct space *space, const char *title, size_t length);

void space_present();
/* BSP kernel-task entry; argument is unused. */
void space_present_task(void *argument);

#endif // PYXIS_OS_SPACE_H
