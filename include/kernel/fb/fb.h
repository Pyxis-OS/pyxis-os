//
// Created by chronium on 9/18/26.
//

#ifndef PYXIS_OS_FB_H
#define PYXIS_OS_FB_H

#include <stdint.h>
#include <stddef.h>

struct framebuffer
{
  uintptr_t address;
  size_t size;
  size_t width;
  size_t height;
};

#endif // PYXIS_OS_FB_H
