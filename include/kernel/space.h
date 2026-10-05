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
  /* Bitmaps over boot CPU indices, space_cpu_words() words each. The ceiling
   * is fixed at boot (empty when init did not start). The effective set starts
   * as the ceiling and narrows only while setup is open; the scheduler queue
   * lock protects both effective_cpus and setup_open. Staging holds a request
   * being validated; only the space's sole process can use it while open. */
  uint64_t *ceiling_cpus;
  uint64_t *effective_cpus;
  uint64_t *affinity_staging;
  bool setup_open;
  struct space *next; /* Registry order; fixed once the scheduler starts. */
};

/* Creates Caelum's space, first in registry order, on CPU 0. BSP only, at boot. */
void space_init(const struct boot_framebuffer *boot_fb);
/* Words in an allowed-CPU bitmap covering every boot CPU index. */
size_t space_cpu_words(void);
/* Appends a workload space titled NAME whose ceiling is CEILING_CPUS. Takes
 * ownership of the kmalloc'd bitmap. Setup starts open. BSP only, before
 * task_schedule(); spaces are never destroyed. Panics on exhaustion. */
struct space *space_create(const char *name, uint64_t *ceiling_cpus);
bool space_ceiling_allows(const struct space *space, size_t cpu_index);
/* Queue lock held, or setup closed (the effective set is then fixed). */
bool space_allows_cpu(const struct space *space, size_t cpu_index);
/* Boot only: writes TEXT to the space's terminal, for a space that cannot start. */
void space_report(struct space *space, const char *text);

/* Copies a validated title without allocation. Preserves IF. */
bool space_set_title(struct space *space, const char *title, size_t length);

void space_present();
/* BSP kernel-task entry; argument is unused. */
void space_present_task(void *argument);

#endif // PYXIS_OS_SPACE_H
