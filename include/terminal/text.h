#ifndef TERMINAL_TEXT_H
#define TERMINAL_TEXT_H

#include <stddef.h>
#include <stdint.h>

#define TERMINAL_REPLACEMENT UINT16_C(0xfffd)

/* One terminal stream, including partial sequences across output calls. */
struct terminal_utf8 {
  uint32_t value;
  uint8_t count;
  uint8_t remaining;
};

/* Text bytes only: callers handle raw controls and CSI separately. A completed
 * unsupported scalar yields one replacement; invalid bytes yield one each.
 * At most four characters result when a byte interrupts a pending sequence. */
size_t terminal_utf8_decode(struct terminal_utf8 *state, uint8_t byte,
    uint16_t output[4]);
/* A control or explicit stream boundary interrupts an incomplete sequence. */
size_t terminal_utf8_flush(struct terminal_utf8 *state);
/* Bizcat's fixed single-cell repertoire. Other scalars map to its placeholder. */
uint8_t terminal_character_glyph(uint16_t character);
/* Cells contain supported BMP scalars or TERMINAL_REPLACEMENT. */
size_t terminal_character_encode(uint16_t character, char output[3]);

#endif
