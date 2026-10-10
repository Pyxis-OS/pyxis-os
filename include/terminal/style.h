#ifndef TERMINAL_STYLE_H
#define TERMINAL_STYLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TERMINAL_CSI_PARAMETERS 16

typedef uint32_t terminal_color;

/* Indices 0..255 have no tag; RGB occupies the low 24 bits. */
#define TERMINAL_COLOR_RGB UINT32_C(0x01000000)
#define TERMINAL_COLOR_DEFAULT UINT32_C(0x02000000)

enum terminal_attribute {
  TERMINAL_ATTR_BOLD = 1u << 0,
  TERMINAL_ATTR_ITALIC = 1u << 1,
  TERMINAL_ATTR_UNDERLINE = 1u << 2,
  TERMINAL_ATTR_REVERSE = 1u << 3,
};

struct terminal_style {
  terminal_color foreground;
  terminal_color background;
  uint8_t attributes;
};

struct terminal_cell {
  terminal_color foreground;
  terminal_color background;
  unsigned char glyph;
  uint8_t attributes;
};

static_assert(sizeof(struct terminal_cell) == 12, "terminal cell backing contract");

#define TERMINAL_AARDVARK_PALETTE { \
  0x222734, 0xc26265, 0x52aa60, 0xad9b49, 0x487fd4, 0xaf5bd1, 0x269d9a, 0x5a6377, \
  0x3a4152, 0xe48383, 0x75cf84, 0xc7b461, 0x76a8f2, 0xd58bf0, 0x52c4c0, 0xdfe5ee \
}
#define TERMINAL_AARDVARK_FOREGROUND UINT32_C(0xb4bcca)
#define TERMINAL_AARDVARK_BACKGROUND UINT32_C(0x0f141f)

/* PRESENT marks parameters containing digits. Empty ordinary parameters reset;
 * colour operands must be present. Rejection leaves STYLE unchanged. Tokenizers
 * reject colon forms and numeric/parameter overflow before calling this. */
bool terminal_sgr_apply(struct terminal_style *style, const uint16_t *parameters,
    uint16_t present, size_t count);
uint32_t terminal_color_rgb(terminal_color color, const uint32_t palette[16],
    uint32_t default_rgb);

#endif
