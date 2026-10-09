#ifndef KERNEL_DISPLAY_CAPTURE_H
#define KERNEL_DISPLAY_CAPTURE_H

#include <kernel/fb/fb.h>

struct screen_capture_request;

/* BSP executor, IF=0, FORWARDED request. Admit one or finish with refusal.
 * Ownership transfers immediately; no caller access may follow this function. */
void screen_capture_submit(struct screen_capture_request *request);
/* Sole BSP presenter, IF=1, after resizing and before a frame lease. A request
 * received after this boundary waits for a later frame. Allocation uses IF=0. */
void screen_capture_begin(const struct framebuffer *layout, uint64_t generation);
/* Sole presenter during a frame: admission cannot replace active backing. */
bool screen_capture_active(void);
/* Presenter frame lease, IF=1. Tee visible pixel spans into compact storage,
 * then submit those same bytes; device padding follows the ordinary driver. */
void screen_capture_copy(size_t offset, const void *pixels, size_t bytes);
/* Read one visible row span already teed into active capture storage. */
void screen_capture_read(size_t offset, void *pixels, size_t bytes);
/* Active presenter capture only; update compact visible pixels without sending
 * them to scanout. Physical row padding is skipped. */
void screen_capture_copy_only(size_t offset, const void *pixels, size_t bytes);
/* Presenter, IF=1, after ending the lease. Failure also drains a pending request
 * when no frame can start. Clear loans/storage ownership before notifying. */
void screen_capture_finish(bool presented);

#endif
