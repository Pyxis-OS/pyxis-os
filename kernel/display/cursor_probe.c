#include "cursor_probe.h"
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/log.h>

/* All hooks are gated by display.cursor.probe in the sole BSP presenter. */
static bool composed, sampled, visible, previous_visible;
static int64_t x, y, previous_x, previous_y;
static uint64_t started, compositions, copies, moved_frames, visible_frames;

void cursor_probe_begin(void)
{
  if (!started) {
    started = arch_monotonic_ns();
  }
  composed = false;
}

void cursor_probe_compose(void)
{
  if (!composed) {
    composed = true;
    ++compositions;
  }
}

void cursor_probe_copy(void)
{
  ++copies;
}

void cursor_probe_pointer(const struct pointer_frame *frame)
{
  if (frame) {
    x = frame->x;
    y = frame->y;
    visible = frame->visible;
  }
}

void cursor_probe_record(uint64_t frames, uint64_t compose_total, uint64_t copy_total)
{
  if (visible) {
    ++visible_frames;
    moved_frames += sampled && previous_visible && (x != previous_x || y != previous_y);
  }
  previous_x = x;
  previous_y = y;
  previous_visible = visible;
  sampled = true;
  if (frames % 120) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  struct pointer_probe_stats input = pointer_probe_snapshot();
  cpu_restore_interrupts(flags);
  klog("display-cursor-probe: elapsed=%lu ns frames=%lu compositions=%lu cpu-scanout-copies=%lu reports=%lu relative-motion=%lu screen-moves=%lu moved-frames=%lu visible-frames=%lu\n",
      arch_monotonic_ns() - started, frames, compositions, copies, input.reports,
      input.relative_motion, input.screen_moves, moved_frames, visible_frames);
  klog("display-cursor-probe: compose-total=%lu ns copy-total=%lu ns; cumulative elapsed includes preemption; no cursor writes\n",
      compose_total, copy_total);
}
