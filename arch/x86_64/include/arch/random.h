#ifndef ARCH_RANDOM_H
#define ARCH_RANDOM_H

#include <stdbool.h>
#include <stdint.h>

enum arch_random_result { ARCH_RANDOM_OK, ARCH_RANDOM_EMPTY, ARCH_RANDOM_FAILED };
enum arch_random_instruction { ARCH_RANDOM_RDRAND, ARCH_RANDOM_RDSEED, ARCH_RANDOM_INSTRUCTIONS };

/* BSP worker only, including initialization. Test each advertised instruction
 * independently. A disabled instruction remains disabled until reboot. */
bool arch_random_init(void);
bool arch_random_enabled(enum arch_random_instruction instruction);
/* RDSEED first; absence, disablement or carry exhaustion permits RDRAND fallback.
 * FAILED disables at least one instruction. Discard and refill the whole request
 * before retrying with any remaining instruction. No suspect word is returned. */
enum arch_random_result arch_random_word(uint64_t *word);

#endif
