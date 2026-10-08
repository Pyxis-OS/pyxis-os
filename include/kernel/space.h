//
// Created by chronium on 9/18/26.
//

#ifndef PYXIS_OS_SPACE_H
#define PYXIS_OS_SPACE_H

#include <abi/space.h>
#include <stdatomic.h>
#include <kernel/fb/fb.h>
#include <kernel/fb/tty.h>

struct console_object;
struct display_object;
struct keyboard_object;
struct pointer_object;

struct space
{
  char name[SPACE_NAME_MAX + 1]; /* Fixed identity for logs; never a title. */
  char title[SPACE_TITLE_MAX + 1]; /* Access under title_locked after boot. */
  atomic_bool title_locked;
  struct framebuffer *fb;
  struct tty *tty;
  struct keyboard_object *keyboard; /* Space retains the initial reference. */
  struct pointer_object *pointer; /* Space retains the initial reference. */
  struct display_object *display; /* Space retains the initial reference. */
  struct console_object *console; /* Space retains the initial reference. */
  /* Bitmaps over boot CPU indices, space_cpu_words() words each. The ceiling
   * is fixed at creation (empty when init did not start). The effective set starts
   * as the ceiling and narrows only while setup is open; the scheduler queue
   * lock protects both effective_cpus and setup_open. Staging holds a request
   * being validated; only the space's sole process can use it while open. */
  uint64_t *ceiling_cpus;
  uint64_t *effective_cpus;
  uint64_t *affinity_staging;
  bool setup_open;
  /* Registry order. Spaces are only appended, on the BSP, and published with
   * a release store once complete, so BSP readers can walk without a lock. */
  struct space *next;
};

/* Creates Caelum's space, first in registry order, on CPU 0. BSP only, at boot. */
/* Enough room for navigation and one terminal row at the current font. */
bool space_display_size_supported(size_t width, size_t height);
void space_init(void);
/* Caelum's own space, which hosts boot init. */
struct space *space_caelum(void);
/* Words in an allowed-CPU bitmap covering every boot CPU index. */
size_t space_cpu_words(void);
/* NAME: 1..SPACE_NAME_MAX bytes of a-z, 0-9 and '-'. */
bool space_name_valid(const char *name, size_t length);
/* BSP only. True when a published space already uses NAME. */
bool space_name_taken(const char *name);
/* Appends a workload space with a valid, unused NAME and TITLE, whose ceiling
 * is CEILING_CPUS (empty for a space that will not start). Takes ownership of
 * the kmalloc'd bitmap. Setup starts open. BSP only, IF=0; spaces are never
 * destroyed. Panics on exhaustion, as boot always has. */
struct space *space_create(const char *name, const char *title, uint64_t *ceiling_cpus);
bool space_ceiling_allows(const struct space *space, size_t cpu_index);
/* Queue lock held, or setup closed (the effective set is then fixed). */
bool space_allows_cpu(const struct space *space, size_t cpu_index);
/* BSP only: writes TEXT to the space's terminal, for a space that cannot start. */
void space_report(struct space *space, const char *text);
/* BSP only, before any task runs in SPACE: removes its CPUs and shows
 * "space NAME not started: REASON" on its tab and in the log. */
void space_report_unstarted(struct space *space, const char *reason);

/* Copies a validated title without allocation. Preserves IF. */
bool space_set_title(struct space *space, const char *title, size_t length);

/* BSP, IF=0, after display state changes. Update input without releasing capture. */
void space_display_changed(struct space *space, bool discard_input);

/* BSP/IF=0: pointer uses the actual last-drawn clipped navigation layout. */
struct space *space_pointer_active(void);
size_t space_pointer_content_y(void);
struct space *space_pointer_tab(int64_t x, int64_t y);
void space_pointer_select(struct space *space);

void space_present();
/* BSP kernel-task entry; argument is unused. */
void space_present_task(void *argument);

#endif // PYXIS_OS_SPACE_H
