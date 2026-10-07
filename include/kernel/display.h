#ifndef KERNEL_DISPLAY_H
#define KERNEL_DISPLAY_H

#include <kernel/boot.h>
#include <kernel/fb/fb.h>

/* Physical screen, distinct from a space's graphics capability. BSP, before
 * AP startup: retain the validated boot layout and its existing WC mapping. */
void display_init(const struct boot_framebuffer *boot);
/* Immutable for this boot; only the display driver writes address. */
const struct framebuffer *display_layout(void);

/* Sole BSP presenter, after early-console retirement. Successful begin must
 * pair with end, including cancelled frames. Copy checks panic ownership in
 * bounded chunks; end drains WC stores before relinquishing the target. */
bool display_begin_frame(void);
void display_copy(size_t offset, const void *pixels, size_t bytes);
void display_end_frame(void);

/* First panic claimant, any CPU, IF=0, without GS/locks/allocation. Permanently
 * stop normal writes, fence an interrupted local writer or boundedly wait for
 * a remote writer. NULL means serial-only; the returned layout stays mapped. */
const struct framebuffer *display_panic_target(void);

#endif
