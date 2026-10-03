#ifndef ARCH_RANDOM_H
#define ARCH_RANDOM_H

#include <stdbool.h>
#include <stdint.h>

enum arch_random_result { ARCH_RANDOM_OK, ARCH_RANDOM_EMPTY, ARCH_RANDOM_FAILED };

/* BSP worker only, including initialization. Tests every advertised instruction
 * that can supply bytes. Health failure remains latched until reboot. */
bool arch_random_init(void);
bool arch_random_has_rdseed(void);
/* RDSEED first; only carry-clear exhaustion permits RDRAND fallback.
 * Validates full 64-bit words before returning any bytes to the service. */
enum arch_random_result arch_random_word(uint64_t *word);

#endif
