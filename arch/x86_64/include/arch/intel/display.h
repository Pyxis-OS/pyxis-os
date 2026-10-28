#ifndef ARCH_INTEL_DISPLAY_H
#define ARCH_INTEL_DISPLAY_H

#include <kernel/boot.h>

/* Matched-profile boot admission; preserves mode/link state and inherited requests. */
void intel_display_prepare(const struct boot_info *boot);

#endif
