#ifndef KERNEL_DISPLAY_H
#define KERNEL_DISPLAY_H

#include <kernel/boot.h>
#include <kernel/fb/fb.h>

struct pointer_frame;

/* Physical screen, distinct from a space's graphics capability. BSP, before
 * AP startup: retain the boot mapping and prepare the first supported device. */
void display_init(const struct boot_info *boot, const char *size, const char *timing,
                  bool timing_metrics);
/* Sole BSP presenter, IF=1, once. Failure leaves presentation unavailable. */
bool display_start(void);
/* BSP display services: acquisition/presentation fail after backend failure. */
bool display_available(void);
/* BSP interrupt, IF=0: record activity and wake the presenter; no queue work. */
void display_interrupt(void);
/* Stable descriptor address. Read geometry under the output lock at runtime;
 * only the display owner replaces geometry/backing during resize commit. */
const struct framebuffer *display_layout(void);

/* Sole BSP presenter, IF=1, between frame leases. NULL prepare means no change,
 * refusal, or permanent failure (check display_available). A candidate remains
 * private while spaces stage their allocations. Switch leaves the old layout
 * published during hardware waits. Cancel restores scanout when necessary and
 * retires the candidate before freeing it; false retains device-owned storage.
 * Defer cancels and retries latest host geometry even without a fresh event. */
const struct framebuffer *display_resize_prepare(void);
bool display_resize_switch(void);
bool display_resize_cancel(void);
bool display_resize_defer(void);
/* BSP IF=0 under the output lock: publish without waiting or allocation. */
void display_resize_commit(void);
/* IF=1 after logical commit: fence retirement of old GPU storage. */
void display_resize_finish(void);
/* Prevent new resizes while leaving ordinary presentation available. */
void display_resize_disable(void);

/* Sole BSP presenter, after early-console retirement. Successful begin must
 * pair with end, including cancelled frames. Copy checks panic ownership in
 * bounded chunks; end submits/drains the frame before relinquishing the target.
 * Boot and Bochs copies land in a RAM staging frame that end copies to scanout
 * memory, so a presented frame must cover every visible pixel; unwritten parts
 * would show the previous frame. */
bool display_begin_frame(void);
/* Stable backend selection; boot and Bochs use software composition. */
bool display_pointer_hardware(void);
void display_copy(size_t offset, const void *pixels, size_t bytes);
/* FRAME is the leased pointer snapshot, or NULL for cancelled begin. True only
 * after ordinary submission and pointer posting, without panic. Active capture
 * additionally requires matching hardware pointer completion. */
bool display_end_frame(const struct pointer_frame *frame);

/* First panic claimant, any CPU, IF=0, without GS/locks/allocation. Permanently
 * stop normal writes, fence an interrupted local writer or boundedly wait for
 * a remote direct writer. VirtIO returns NULL without device operations.
 * NULL means serial-only; the returned direct layout stays mapped. */
const struct framebuffer *display_panic_target(void);

#endif
