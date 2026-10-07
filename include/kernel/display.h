#ifndef KERNEL_DISPLAY_H
#define KERNEL_DISPLAY_H

#include <kernel/boot.h>
#include <kernel/fb/fb.h>

/* Physical screen, distinct from a space's graphics capability. BSP, before
 * AP startup: retain the boot mapping and prepare the first supported device. */
void display_init(const struct boot_info *boot, const char *size);
/* Sole BSP presenter, IF=1, once. Failure leaves presentation unavailable. */
bool display_start(void);
/* BSP display services: acquisition/presentation fail after backend failure. */
bool display_available(void);
/* BSP interrupt, IF=0: record activity and wake the presenter; no queue work. */
void display_interrupt(void);
/* Immutable for this boot; only the display driver writes address. */
const struct framebuffer *display_layout(void);

/* Sole BSP presenter, after early-console retirement. Successful begin must
 * pair with end, including cancelled frames. Copy checks panic ownership in
 * bounded chunks; end submits/drains the frame before relinquishing the target. */
bool display_begin_frame(void);
void display_copy(size_t offset, const void *pixels, size_t bytes);
void display_end_frame(void);

/* First panic claimant, any CPU, IF=0, without GS/locks/allocation. Permanently
 * stop normal writes, fence an interrupted local writer or boundedly wait for
 * a remote direct writer. VirtIO returns NULL without device operations.
 * NULL means serial-only; the returned direct layout stays mapped. */
const struct framebuffer *display_panic_target(void);

#endif
