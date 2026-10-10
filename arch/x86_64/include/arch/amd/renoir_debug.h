#ifndef ARCH_AMD_RENOIR_DEBUG_H
#define ARCH_AMD_RENOIR_DEBUG_H

#include <stdint.h>

/* Documented OTG register window in assigned BAR5, not the unsized BAR extent. */
#define RENOIR_DEBUG_REGISTER_BAR 5
#define RENOIR_DEBUG_WINDOW_OFFSET UINT32_C(0x13000)
#define RENOIR_DEBUG_WINDOW_BYTES UINT32_C(0x2000)

#endif
