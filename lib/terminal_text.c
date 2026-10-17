#include <terminal/text.h>

#define REPLACEMENT_GLYPH UINT16_C(0xff)

/* Slots after Bizcat's own 256, in the order scripts/bizcat-glyphs.py writes
 * them. */
struct glyph_range {
  uint16_t first;
  uint16_t last;
  uint16_t slot;
};

static const struct glyph_range generated_ranges[] = {
  {0x2500, 0x257f, 0x100}, /* box drawing */
  {0x2580, 0x259f, 0x180}, /* block elements */
  {0x2800, 0x28ff, 0x1a0}, /* braille patterns */
};

/* The atlas rearranges Latin-1; zero means that no matching glyph exists. */
static const uint8_t latin1_glyphs[96] = {
  0x20, 0xa0, 0xa4, 0xa5, 0, 0xa7, 0, 0,
  0, 0xaf, 0xb4, 0xa2, 0, 0, 0xae, 0,
  0xa9, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0xb5, 0xa3, 0, 0, 0, 0xa1,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xb0, 0xce,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd,
  0xdc, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xaa,
  0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdd, 0xfe,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xb1, 0xee,
  0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed,
  0xfc, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xab,
  0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfd, 0xfb,
};

uint16_t terminal_character_glyph(uint16_t character)
{
  if (character >= 0x20 && character <= 0x7e) {
    return (uint8_t)character;
  }
  if (character >= 0xa0 && character <= 0xff) {
    uint8_t glyph = latin1_glyphs[character - 0xa0];
    return glyph ? glyph : REPLACEMENT_GLYPH;
  }
  for (size_t i = 0; i < sizeof(generated_ranges) / sizeof(generated_ranges[0]); ++i) {
    const struct glyph_range *range = &generated_ranges[i];
    if (character >= range->first && character <= range->last) {
      return (uint16_t)(range->slot + character - range->first);
    }
  }
  switch (character) {
  case 0x2190: return 0x10;
  case 0x2191: return 0x12;
  case 0x2192: return 0x11;
  case 0x2193: return 0x13;
  default: return REPLACEMENT_GLYPH;
  }
}

size_t terminal_utf8_flush(struct terminal_utf8 *state)
{
  size_t count = state->count;
  *state = (struct terminal_utf8){0};
  return count;
}

size_t terminal_utf8_decode(struct terminal_utf8 *state, uint8_t byte,
    uint16_t output[4])
{
  size_t count = 0;
  if (state->remaining) {
    if (byte >= state->lower && byte <= state->upper) {
      state->value = state->value << 6 | (byte & 0x3f);
      ++state->count;
      state->lower = 0x80;
      state->upper = 0xbf;
      if (--state->remaining) {
        return 0;
      }
      uint32_t value = state->value;
      terminal_utf8_flush(state);
      output[count++] = value <= UINT16_MAX &&
          terminal_character_glyph((uint16_t)value) != REPLACEMENT_GLYPH ?
          (uint16_t)value : TERMINAL_REPLACEMENT;
      return count;
    }
    count = terminal_utf8_flush(state);
    for (size_t i = 0; i < count; ++i) {
      output[i] = TERMINAL_REPLACEMENT;
    }
  }

  if (byte < 0x80) {
    output[count++] = byte;
  } else if (byte >= 0xc2 && byte <= 0xf4) {
    state->remaining = byte < 0xe0 ? 1 : byte < 0xf0 ? 2 : 3;
    state->value = byte & (0x3f >> state->remaining);
    state->count = 1;
    /* Exclude overlong forms, surrogates and values beyond U+10FFFF as soon
     * as the second byte makes the prefix impossible. */
    state->lower = byte == 0xe0 ? 0xa0 : byte == 0xf0 ? 0x90 : 0x80;
    state->upper = byte == 0xed ? 0x9f : byte == 0xf4 ? 0x8f : 0xbf;
  } else {
    output[count++] = TERMINAL_REPLACEMENT;
  }
  return count;
}

size_t terminal_character_encode(uint16_t character, char output[3])
{
  if (character < 0x80) {
    output[0] = (char)character;
    return 1;
  }
  if (character < 0x800) {
    output[0] = (char)(0xc0 | character >> 6);
    output[1] = (char)(0x80 | (character & 0x3f));
    return 2;
  }
  output[0] = (char)(0xe0 | character >> 12);
  output[1] = (char)(0x80 | (character >> 6 & 0x3f));
  output[2] = (char)(0x80 | (character & 0x3f));
  return 3;
}
