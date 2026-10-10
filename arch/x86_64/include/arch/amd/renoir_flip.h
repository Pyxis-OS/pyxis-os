#ifndef ARCH_AMD_RENOIR_FLIP_H
#define ARCH_AMD_RENOIR_FLIP_H
#include <kernel/fb/fb.h>
#include <kernel/boot.h>
enum renoir_flip_state {
  RENOIR_FLIP_OFF, RENOIR_FLIP_READY, RENOIR_FLIP_PENDING,
  RENOIR_FLIP_FALLBACK, RENOIR_FLIP_FAILED,
};
/* BSP IF=0, before AP startup; private surfaces and register mappings retained. */
bool renoir_flip_prepare(const struct boot_info *boot, const struct framebuffer *gop, bool metrics);
void renoir_flip_cancel_prepare(void);
enum renoir_flip_state renoir_flip_state(void);
const struct framebuffer *renoir_flip_surface(unsigned index);
const struct framebuffer *renoir_flip_back(void);
/* Sole presenter with direct-writer claim; WC stores already fenced. */
enum renoir_flip_state renoir_flip_submit(void);
/* Sole presenter IF=1 without writer claim. No register writes. */
enum renoir_flip_state renoir_flip_poll(void);
#endif
