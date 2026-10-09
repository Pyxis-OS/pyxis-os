#ifndef KERNEL_POINTER_PRESENT_H
#define KERNEL_POINTER_PRESENT_H

#include <kernel/fb/fb.h>

struct pointer_frame;

/* Sole BSP presenter, IF=1, within a display frame lease. FRAME retains its
 * immutable image through display and capture completion. Software spans blend
 * before the capture tee; hardware sends underlying spans once and blends only
 * into an active capture. All spans are pixel-aligned native pixels. */
void pointer_present_copy(const struct framebuffer *layout,
    const struct pointer_frame *frame, size_t offset, const void *pixels, size_t bytes);

#endif
