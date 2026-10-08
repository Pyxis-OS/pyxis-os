/*
 * Adapted from OpenBSD arc4random.c revision 1.58.
 * Copyright (c) 1996, David Mazieres <dm@uun.org>
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
 * Copyright (c) 2013, Markus Friedl <markus@openbsd.org>
 * Copyright (c) 2014, Theo de Raadt <deraadt@openbsd.org>
 * ISC license; the complete permission and warranty notice is in NOTICE.
 */

#include <abi/random.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/random/generator.h>

#define RANDOM_RESEED_BASE (1024 * 1024)
#define RANDOM_RESEED_NS UINT64_C(60000000000)

static void generator_rekey(struct random_generator *generator,
                            const uint8_t *seed)
{
  for (size_t i = 0; i < sizeof(generator->buffer);
       i += CHACHA20_BLOCK_BYTES) {
    chacha20_block(&generator->state, generator->buffer + i);
  }
  if (seed) {
    for (size_t i = 0; i < RANDOM_GENERATOR_SEED_BYTES; ++i) {
      generator->buffer[i] ^= seed[i];
    }
  }

  /* Replace the old context before exposing any output from its stream. */
  chacha20_init(&generator->state, generator->buffer,
                generator->buffer + CHACHA20_KEY_BYTES);
  memzero_explicit(generator->buffer, RANDOM_GENERATOR_SEED_BYTES);
  generator->available = sizeof(generator->buffer) - RANDOM_GENERATOR_SEED_BYTES;
}

bool random_generator_due(const struct random_generator *generator,
                          size_t requested, uint64_t now)
{
  return !generator->seeded || generator->budget <= requested ||
         now - generator->seeded_at >= RANDOM_RESEED_NS;
}

void random_generator_seed(struct random_generator *generator,
                           const uint8_t seed[RANDOM_GENERATOR_SEED_BYTES],
                           uint64_t now)
{
  if (generator->seeded) {
    generator_rekey(generator, seed);
  } else {
    chacha20_init(&generator->state, seed, seed + CHACHA20_KEY_BYTES);
  }
  generator->available = 0;
  memzero_explicit(generator->buffer, sizeof(generator->buffer));

  /* OpenBSD consumes a whole block for its private four-byte budget draw. */
  uint8_t block[CHACHA20_BLOCK_BYTES];
  chacha20_block(&generator->state, block);
  uint32_t fuzz = (uint32_t)block[0] | (uint32_t)block[1] << 8 |
                  (uint32_t)block[2] << 16 | (uint32_t)block[3] << 24;
  generator->budget = RANDOM_RESEED_BASE + fuzz % RANDOM_RESEED_BASE;
  memzero_explicit(block, sizeof(block));
  memzero_explicit(&fuzz, sizeof(fuzz));
  generator->seeded_at = now;
  generator->seeded = true;
}

void random_generator_read(struct random_generator *generator, void *bytes,
                           size_t length)
{
  KASSERT(generator->seeded);
  KASSERT(length <= RANDOM_MAX_BYTES);
  KASSERT(generator->budget > length);
  generator->budget -= length;

  uint8_t *output = bytes;
  while (length) {
    if (generator->available) {
      size_t count = length < generator->available ? length : generator->available;
      uint8_t *buffered = generator->buffer + sizeof(generator->buffer) -
                          generator->available;
      memcpy(output, buffered, count);
      memzero_explicit(buffered, count);
      output += count;
      length -= count;
      generator->available -= count;
    }
    if (!generator->available) {
      generator_rekey(generator, NULL);
    }
  }
}

void random_generator_clear(struct random_generator *generator)
{
  memzero_explicit(generator, sizeof(*generator));
}
