#ifndef ARCH_INTEL_DISPLAY_H
#define ARCH_INTEL_DISPLAY_H

#include <kernel/boot.h>
#include <kernel/fb/fb.h>

/* Matched-profile boot admission; preserves mode/link state and inherited requests. */
void intel_display_prepare(const struct boot_info *boot);

enum intel_flip_state {
  INTEL_FLIP_OFF, INTEL_FLIP_READY, INTEL_FLIP_DEFERRED,
  INTEL_FLIP_PENDING, INTEL_FLIP_FAILED,
};
/* Sole presenter; all layouts/mappings remain pinned, including panic. */
enum intel_flip_state intel_flip_state(void);
const struct framebuffer *intel_flip_surface(unsigned index);
const struct framebuffer *intel_flip_back(void);
void intel_flip_cancel_prepare(void);
/* Writer claim held; completed WC stores precede the synchronous arm. */
enum intel_flip_state intel_flip_submit(void);
enum intel_flip_state intel_flip_retry(void);
/* Writer-free, read-only hardware observation; DEFERRED has not armed yet. */
enum intel_flip_state intel_flip_poll(void);

#endif
