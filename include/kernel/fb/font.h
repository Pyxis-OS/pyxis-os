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
  uint16_t base_glyphs; /* Glyphs in data; later indices are in extension. */
  const uint8_t *data;
  const uint8_t *extension;
};

extern const struct font bizcat;

/* The index must not exceed max_glyph. */
static inline const uint8_t *font_glyph(const struct font *font, uint16_t index)
{
  if (index < font->base_glyphs) {
    return font->data + (size_t)index * font->stride;
  }
  return font->extension + (size_t)(index - font->base_glyphs) * font->stride;
}

#endif
