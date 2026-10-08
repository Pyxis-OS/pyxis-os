#ifndef KERNEL_RANDOM_GENERATOR_H
#define KERNEL_RANDOM_GENERATOR_H

#include <kernel/random/chacha.h>
#include <stddef.h>

#define RANDOM_GENERATOR_SEED_BYTES (CHACHA20_KEY_BYTES + CHACHA20_NONCE_BYTES)

/* Exclusively owned by the BSP random worker; never copied or rolled back. */
struct random_generator {
  struct chacha_state state;
  uint8_t buffer[1024];
  size_t available, budget;
  uint64_t seeded_at;
  bool seeded;
};

/* now is monotonic nanoseconds. The worker must latch a required reseed until
 * a complete seed succeeds, including after a failed collection attempt. */
bool random_generator_due(const struct random_generator *generator,
                          size_t requested, uint64_t now);
/* The worker retains ownership of seed and must explicitly erase it. */
void random_generator_seed(struct random_generator *generator,
                           const uint8_t seed[RANDOM_GENERATOR_SEED_BYTES],
                           uint64_t now);
/* A successful seed and sufficient budget are preconditions. Charge before
 * extraction, even when the worker subsequently discards the reply. */
void random_generator_read(struct random_generator *generator, void *bytes,
                           size_t length);
void random_generator_clear(struct random_generator *generator);

#endif
