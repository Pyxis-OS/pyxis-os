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
  size_t pitch;
  
  size_t width;
  size_t height;

  uint8_t red_shift;
  uint8_t green_shift;
  uint8_t blue_shift;
};

#endif // PYXIS_OS_FB_H
