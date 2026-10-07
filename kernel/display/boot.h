#ifndef KERNEL_DISPLAY_BOOT_H
#define KERNEL_DISPLAY_BOOT_H

#include <kernel/boot.h>
#include <kernel/fb/fb.h>

const struct framebuffer *boot_display_init(const struct boot_framebuffer *boot);
void boot_display_copy(size_t offset, const void *pixels, size_t bytes);

#endif
