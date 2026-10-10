#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/boot/options.h>
#include <kernel/display.h>
#include <kernel/input.h>
#include <kernel/log.h>
#include <kernel/input/synthetic_pointer.h>
#include <kernel/object/display.h>
#include <kernel/panic.h>
#include <kernel/pointer.h>
#include <kernel/space.h>
#include <kernel/ui/power_overlay.h>

#define SYNTHETIC_SECOND_NS UINT64_C(1000000000)
#define SYNTHETIC_REPORT_INTERVAL_NS UINT64_C(16666667)
#define SYNTHETIC_PERIOD_NS (10 * SYNTHETIC_SECOND_NS)
#define SYNTHETIC_INSET 64
#define SYNTHETIC_MAX_RADIUS 128
#define SYNTHETIC_SINE_SCALE 32768
#define SYNTHETIC_QUARTER_PHASE 32768
#define SYNTHETIC_FULL_PHASE (4 * SYNTHETIC_QUARTER_PHASE)
#define SYNTHETIC_QUARTER_STEPS 32

enum synthetic_window {
  SYNTHETIC_SETTLE,
  SYNTHETIC_IDLE_1,
  SYNTHETIC_MOTION_1,
  SYNTHETIC_IDLE_2,
  SYNTHETIC_MOTION_2,
  SYNTHETIC_DONE,
};

static const char *const window_names[] = {
  "settle", "idle-1", "motion-1", "idle-2", "motion-2", "done",
};
static const uint64_t window_starts[] = {
  0, 10 * SYNTHETIC_SECOND_NS, 40 * SYNTHETIC_SECOND_NS,
  70 * SYNTHETIC_SECOND_NS, 100 * SYNTHETIC_SECOND_NS,
  130 * SYNTHETIC_SECOND_NS,
};
/* Quarter sine, Q15, with linear interpolation between 32 equal intervals. */
static const uint16_t quarter_sine[] = {
  0, 1608, 3212, 4808, 6393, 7962, 9512, 11039,
  12540, 14010, 15447, 16846, 18205, 19520, 20788, 22006,
  23170, 24279, 25330, 26320, 27246, 28106, 28899, 29622,
  30274, 30853, 31357, 31786, 32138, 32413, 32610, 32729, 32768,
};

static struct input_source synthetic_source = {.synthetic = true};
static struct space *target_space;
static size_t target_width, target_height, target_content_y;
static uint64_t target_geometry_generation;
static int64_t center_x, center_y, previous_x, previous_y;
static int32_t radius;
static uint32_t last_emitted_phase, motion_start_phase;
static uint64_t started_at, reports, last_motion_slot;
static enum synthetic_window window;
static bool started, finished;

static bool ordinary_terminal(struct space *space, uint32_t physical_buttons)
{
  return space && space->tty && space->pointer && space->display &&
      !space->display->visible && !pointer_locked() && !power_overlay_shown() &&
      !physical_buttons;
}

static bool target_unchanged(uint32_t physical_buttons)
{
  const struct framebuffer *layout = display_layout();
  return display_available() && space_pointer_active() == target_space &&
      ordinary_terminal(target_space, physical_buttons) &&
      layout->width == target_width && layout->height == target_height &&
      space_pointer_content_y() == target_content_y &&
      target_space->tty->geometry_generation == target_geometry_generation;
}

static void stop_schedule(bool aborted, uint64_t elapsed)
{
  input_source_lost(&synthetic_source);
  finished = true;
  display_cursor_probe_boundary(aborted ? "aborted" : "done",
      aborted ? elapsed : window_starts[SYNTHETIC_DONE], elapsed, reports);
}

static bool capture_geometry(void)
{
  const struct framebuffer *layout = display_layout();
  target_width = layout->width;
  target_height = layout->height;
  target_content_y = space_pointer_content_y();
  target_geometry_generation = target_space->tty->geometry_generation;
  if (target_width > INT32_MAX || target_height > INT32_MAX ||
      target_content_y >= target_height) {
    return false;
  }
  size_t body_height = target_height - target_content_y;
  if (target_width <= 2 * SYNTHETIC_INSET + 2 ||
      body_height <= 2 * SYNTHETIC_INSET + 2) {
    return false;
  }
  center_x = target_width / 2;
  center_y = target_content_y + body_height / 2;
  size_t smaller_extent = target_width < body_height ? target_width : body_height;
  radius = smaller_extent / 4;
  if (radius > SYNTHETIC_MAX_RADIUS) {
    radius = SYNTHETIC_MAX_RADIUS;
  }
  int64_t horizontal_room = (int64_t)target_width - SYNTHETIC_INSET - 1 - center_x;
  int64_t vertical_room = (int64_t)target_height - SYNTHETIC_INSET - 1 - center_y;
  if (radius > horizontal_room) {
    radius = horizontal_room;
  }
  if (radius > vertical_room) {
    radius = vertical_room;
  }
  return radius > 0;
}

static int32_t sine(uint32_t phase)
{
  phase %= SYNTHETIC_FULL_PHASE;
  uint32_t quadrant = phase / SYNTHETIC_QUARTER_PHASE;
  uint32_t offset = phase % SYNTHETIC_QUARTER_PHASE;
  if (quadrant & 1) {
    offset = SYNTHETIC_QUARTER_PHASE - offset;
  }
  uint32_t step_phase = SYNTHETIC_QUARTER_PHASE / SYNTHETIC_QUARTER_STEPS;
  uint32_t index = offset / step_phase;
  int32_t value = quarter_sine[index];
  if (index < SYNTHETIC_QUARTER_STEPS) {
    uint32_t fraction = offset % step_phase;
    value += (quarter_sine[index + 1] - value) * fraction / step_phase;
  }
  return quadrant >= 2 ? -value : value;
}

static void begin_schedule(uint64_t now)
{
  target_space = space_pointer_active();
  started_at = now;
  started = true;
  window = SYNTHETIC_SETTLE;
  if (!capture_geometry()) {
    stop_schedule(true, 0);
    return;
  }
  input_source_attach(&synthetic_source, false, true);
  klog("pointer-synthetic: target=%s body=%lux%lu at-y=%lu center=%ld,%ld radius=%d period=10 s; no clicks/wheel/focus changes\n",
      target_space->name, target_width, target_height - target_content_y,
      target_content_y, center_x, center_y, radius);
  display_cursor_probe_boundary("settle", 0, 0, reports);
  struct pointer_frame frame;
  pointer_frame_snapshot(&frame);
  previous_x = center_x + radius;
  previous_y = center_y;
  int32_t dx = previous_x - frame.x;
  int32_t dy = previous_y - frame.y;
  pointer_frame_release(&frame);
  input_pointer_report(&synthetic_source, dx, dy, 0, 0);
  ++reports;
}

static bool report_motion(uint64_t motion_elapsed)
{
  uint32_t phase = (motion_elapsed % SYNTHETIC_PERIOD_NS) *
      SYNTHETIC_FULL_PHASE / SYNTHETIC_PERIOD_NS;
  phase = (motion_start_phase + phase) % SYNTHETIC_FULL_PHASE;
  int64_t x = center_x + (int64_t)radius *
      sine(phase + SYNTHETIC_QUARTER_PHASE) / SYNTHETIC_SINE_SCALE;
  int64_t y = center_y + (int64_t)radius * sine(phase) / SYNTHETIC_SINE_SCALE;
  int32_t dx = x - previous_x;
  int32_t dy = y - previous_y;
  struct pointer_frame frame;
  pointer_frame_snapshot(&frame);
  int64_t moved_x = frame.x + dx;
  int64_t moved_y = frame.y + dy;
  pointer_frame_release(&frame);
  /* Physical motion remains additive; never compensate for it or enter chrome. */
  if (moved_x < SYNTHETIC_INSET ||
      moved_x >= (int64_t)target_width - SYNTHETIC_INSET ||
      moved_y < (int64_t)target_content_y + SYNTHETIC_INSET ||
      moved_y >= (int64_t)target_height - SYNTHETIC_INSET) {
    return false;
  }
  input_pointer_report(&synthetic_source, dx, dy, 0, 0);
  previous_x = x;
  previous_y = y;
  last_emitted_phase = phase;
  ++reports;
  return true;
}

void pointer_synthetic_tick(uint32_t physical_buttons)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (finished || !boot_options_get()->pointer_synthetic) {
    return;
  }
  if (!started) {
    if (display_available() && ordinary_terminal(space_pointer_active(), physical_buttons)) {
      begin_schedule(arch_monotonic_ns());
    }
    return;
  }
  uint64_t elapsed = arch_monotonic_ns() - started_at;
  if (!target_unchanged(physical_buttons)) {
    stop_schedule(true, elapsed);
    return;
  }
  while (window < SYNTHETIC_DONE && elapsed >= window_starts[window + 1]) {
    ++window;
    if (window == SYNTHETIC_DONE) {
      stop_schedule(false, elapsed);
      return;
    }
    display_cursor_probe_boundary(window_names[window], window_starts[window], elapsed, reports);
    last_motion_slot = UINT64_MAX;
    if (window == SYNTHETIC_MOTION_1 || window == SYNTHETIC_MOTION_2) {
      /* Resume from the held endpoint even if service missed the prior tail. */
      motion_start_phase = last_emitted_phase;
    }
  }
  if (window != SYNTHETIC_MOTION_1 && window != SYNTHETIC_MOTION_2) {
    return;
  }
  uint64_t motion_elapsed = elapsed - window_starts[window];
  uint64_t slot = motion_elapsed / SYNTHETIC_REPORT_INTERVAL_NS;
  if (slot == last_motion_slot) {
    return;
  }
  last_motion_slot = slot;
  if (!report_motion(motion_elapsed)) {
    stop_schedule(true, elapsed);
  }
}
