//
// Created by chronium on 9/18/26.
//

#ifndef PYXIS_OS_SPACE_H
#define PYXIS_OS_SPACE_H

#include <kernel/fb/fb.h>
#include <kernel/boot.h>

struct space
{
  char *name;
  struct framebuffer *fb;
};

void space_init_all(struct boot_framebuffer *boot_fb);

#endif // PYXIS_OS_SPACE_H
