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

/* Pack 0xRRGGBB using the framebuffer's channel positions. */
uint32_t framebuffer_color(const struct framebuffer *fb, uint32_t rgb);

/* Rectangle colors are 0xRRGGBB, like TTY foreground/background colors. */
void fb_rect(struct framebuffer *fb, size_t x, size_t y,
    size_t width, size_t height, uint32_t color);
void fb_fill_rect(struct framebuffer *fb, size_t x, size_t y,
    size_t width, size_t height, uint32_t color);

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#endif // PYXIS_OS_FB_H
