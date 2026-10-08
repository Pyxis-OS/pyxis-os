/*
 * Adapted from OpenBSD chacha_private.h revision 1.3, chacha-merged.c
 * version 20080118 by D. J. Bernstein. Public domain; see NOTICE.
 */

#include <kernel/memory.h>
#include <kernel/random/chacha.h>

static uint32_t load_little_u32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
         (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void store_little_u32(uint8_t *bytes, uint32_t word)
{
  bytes[0] = word;
  bytes[1] = word >> 8;
  bytes[2] = word >> 16;
  bytes[3] = word >> 24;
}

static uint32_t rotate_left(uint32_t word, unsigned bits)
{
  return (word << bits) | (word >> (32 - bits));
}

static void quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
  *a += *b;
  *d = rotate_left(*d ^ *a, 16);
  *c += *d;
  *b = rotate_left(*b ^ *c, 12);
  *a += *b;
  *d = rotate_left(*d ^ *a, 8);
  *c += *d;
  *b = rotate_left(*b ^ *c, 7);
}

void chacha20_init(struct chacha_state *state,
                   const uint8_t key[CHACHA20_KEY_BYTES],
                   const uint8_t nonce[CHACHA20_NONCE_BYTES])
{
  static const uint8_t sigma[16] = "expand 32-byte k";

  memzero_explicit(state, sizeof(*state));
  for (size_t i = 0; i < 4; ++i) {
    state->words[i] = load_little_u32(sigma + i * 4);
  }
  for (size_t i = 0; i < CHACHA20_KEY_BYTES / 4; ++i) {
    state->words[4 + i] = load_little_u32(key + i * 4);
  }
  state->words[14] = load_little_u32(nonce);
  state->words[15] = load_little_u32(nonce + 4);
}

void chacha20_block(struct chacha_state *state,
                    uint8_t bytes[CHACHA20_BLOCK_BYTES])
{
  uint32_t words[16];
  memcpy(words, state->words, sizeof(words));

  for (unsigned rounds = 20; rounds; rounds -= 2) {
    quarter_round(&words[0], &words[4], &words[8], &words[12]);
    quarter_round(&words[1], &words[5], &words[9], &words[13]);
    quarter_round(&words[2], &words[6], &words[10], &words[14]);
    quarter_round(&words[3], &words[7], &words[11], &words[15]);
    quarter_round(&words[0], &words[5], &words[10], &words[15]);
    quarter_round(&words[1], &words[6], &words[11], &words[12]);
    quarter_round(&words[2], &words[7], &words[8], &words[13]);
    quarter_round(&words[3], &words[4], &words[9], &words[14]);
  }
  for (size_t i = 0; i < 16; ++i) {
    words[i] += state->words[i];
    store_little_u32(bytes + i * 4, words[i]);
  }
  if (++state->words[12] == 0) {
    ++state->words[13];
  }
  memzero_explicit(words, sizeof(words));
}
