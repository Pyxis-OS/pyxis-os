#ifndef KERNEL_RANDOM_CHACHA_H
#define KERNEL_RANDOM_CHACHA_H

#include <stdint.h>

#define CHACHA20_KEY_BYTES 32
#define CHACHA20_NONCE_BYTES 8
#define CHACHA20_BLOCK_BYTES 64

struct chacha_state {
  uint32_t words[16];
};

/* OpenBSD layout: words 12/13 are a 64-bit counter, 14/15 the nonce.
 * While word 13 is zero, this is the RFC 8439 block layout with a zero
 * leading nonce word. The generator reinitializes within 17 blocks. */
void chacha20_init(struct chacha_state *state,
                   const uint8_t key[CHACHA20_KEY_BYTES],
                   const uint8_t nonce[CHACHA20_NONCE_BYTES]);
void chacha20_block(struct chacha_state *state,
                    uint8_t bytes[CHACHA20_BLOCK_BYTES]);

#endif
