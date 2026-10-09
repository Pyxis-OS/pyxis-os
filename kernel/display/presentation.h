#ifndef KERNEL_DISPLAY_PRESENTATION_H
#define KERNEL_DISPLAY_PRESENTATION_H

/* Sole BSP presenter, IF=1, after end relinquishes the direct writer. A pending
 * frame retains composition, application/pointer leases and capture storage.
 * Poll returns true only after confirmation or a complete fallback copy. */
bool display_frame_pending(void);
bool display_frame_poll(void);

#endif
