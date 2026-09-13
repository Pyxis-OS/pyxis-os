#ifndef KERNEL_FB_FONT_H
#define KERNEL_FB_FONT_H

#include <stddef.h>
#include <stdint.h>

struct font {
  const char *name;
  size_t width;
  size_t height;
  size_t stride; /* Bytes per glyph. */
  uint16_t max_glyph; /* Inclusive glyph index. */
  const uint8_t *data;
};

extern const struct font bizcat;

#endif
