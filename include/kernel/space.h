//
// Created by chronium on 9/18/26.
//

#ifndef PYXIS_OS_SPACE_H
#define PYXIS_OS_SPACE_H

#include <kernel/fb/fb.h>
#include <kernel/fb/tty.h>
#include <kernel/boot.h>

struct space
{
  char *name;
  struct framebuffer *fb;
  struct tty *tty;
};

void space_init_all(const struct boot_framebuffer *boot_fb);

void space_present();
/* BSP kernel-task entry; argument is unused. */
void space_present_task(void *argument);
void space_switch(size_t index);

#endif // PYXIS_OS_SPACE_H
