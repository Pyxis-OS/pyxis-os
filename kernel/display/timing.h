#ifndef KERNEL_DISPLAY_TIMING_H
#define KERNEL_DISPLAY_TIMING_H

#include <kernel/boot.h>

/* Private BSP-owned observation, not a program timing or presentation ABI.
 * Hardware timing and a copy's admission are deliberately separate. */
struct display_timing_capability {
  bool hardware;
  uint64_t frame_sequence, timestamp_ns, period_ns, uncertainty_ns;
};

void display_timing_prepare(const struct boot_info *boot, bool firmware_backend,
                           const char *option);
void display_timing_start(void);
void display_timing_wait(void);
bool display_timing_front_write(void);
void display_timing_write_stamp(uint64_t stamp);
void display_timing_progress(size_t rows);
void display_timing_finish(void);
/* Zero preserves the presenter's existing software deadline. */
uint64_t display_timing_next_deadline(void);
const struct display_timing_capability *display_timing_capability(void);

#endif
