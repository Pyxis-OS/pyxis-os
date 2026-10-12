#ifndef KERNEL_INPUT_TOUCHPAD_H
#define KERNEL_INPUT_TOUCHPAD_H

#include <stdint.h>

struct mouse_profile;
struct touchpad_sample;

#define TOUCHPAD_SCROLL_MM_PER_DETENT 8

struct touchpad_motion {
  int32_t dx, dy, scroll_x, scroll_y;
  bool reset;
};

/* BSP, IF=0, retained state; no physical-button ownership. Geometry is copied
 * at initialization. Scroll deltas are device counts toward right/down;
 * movement is display pixels. reset discards caller scroll fractions only. */
void touchpad_init(const struct mouse_profile *profile);
/* Transport loss preserves rejection latches and requires a confirmed lift. */
void touchpad_reset(void);
void touchpad_report(const struct touchpad_sample *sample,
    uint64_t last_typing_ns, uint64_t now, struct touchpad_motion *motion);
void touchpad_service(uint64_t now);

#endif
