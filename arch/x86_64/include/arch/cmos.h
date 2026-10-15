#ifndef ARCH_CMOS_H
#define ARCH_CMOS_H
#include <stdint.h>

/* BSP only; serializes one bounded legacy CMOS transaction with IF=0.
 * Register indices are 7-bit, leaving legacy NMIs unmasked. No CMOS access
 * from NMI handlers. Future RTC users must share this owner. */
bool arch_cmos_transfer(bool write, uint64_t offset, unsigned bytes, uint64_t *value);
#endif
