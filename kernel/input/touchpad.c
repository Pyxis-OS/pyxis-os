#include <kernel/boot/options.h>
#include <kernel/input/touchpad.h>
#include <kernel/log.h>
#include <kernel/mouse.h>

#define TOUCHPAD_SECOND_NS UINT64_C(1000000000)
#define TOUCHPAD_TYPING_NS (TOUCHPAD_SECOND_NS / 2)
#define TOUCHPAD_PRESSURE_ON 30
#define TOUCHPAD_PRESSURE_OFF 25
#define TOUCHPAD_PALM_PRESSURE 220
#define TOUCHPAD_PALM_WIDTH 12
#define TOUCHPAD_EDGE_MM 3
#define TOUCHPAD_INWARD_MM 2
#define TOUCHPAD_PIXELS_PER_MM 6
#define TOUCHPAD_STABLE_SAMPLES 2
#define TOUCHPAD_DIAGNOSTIC_INTERVAL_NS (5 * TOUCHPAD_SECOND_NS)
#define TOUCHPAD_DIAGNOSTIC_SETTLE_NS (10 * TOUCHPAD_SECOND_NS)
#define TOUCHPAD_DIAGNOSTIC_WINDOW_NS (20 * TOUCHPAD_SECOND_NS)

#define TOUCHPAD_EDGE_LEFT (1u << 0)
#define TOUCHPAD_EDGE_RIGHT (1u << 1)
#define TOUCHPAD_EDGE_BOTTOM (1u << 2)
#define TOUCHPAD_EDGE_TOP (1u << 3)

/* These are authored initial tuning, pending native qualification. Pressure
 * and basic-W width are device hints, not universal force/contact scales. */
enum touchpad_rejection {
  TOUCHPAD_REJECT_LIFT,
  TOUCHPAD_REJECT_PRESSURE,
  TOUCHPAD_REJECT_INVALID,
  TOUCHPAD_REJECT_COUNT,
  TOUCHPAD_REJECT_PALM,
  TOUCHPAD_REJECT_TYPING,
  TOUCHPAD_REJECT_EDGE,
  TOUCHPAD_REJECT_LOSS,
  TOUCHPAD_REJECT_COUNT_TOTAL,
};

static struct mouse_profile geometry;
static bool ready, present, admitted, palm, typing_quarantine;
static bool baseline, typing_blocked, loss_quarantine, origin_known;
static uint8_t fingers, stable_samples;
static unsigned origin_edges;
static int32_t origin_x, origin_y, previous_x, previous_y;
static int64_t motion_fraction_x, motion_fraction_y;
static int64_t scroll_pending_x, scroll_pending_y;
static bool scroll_active_x, scroll_active_y;
static uint64_t seen_typing_ns;

static const char *const diagnostic_windows[] = {
  "settle", "normal-finger", "edge-entry", "resting-palms", "typing",
  "finger-transitions-physical-drag", "TrackPoint-with-palm", "two-finger-scroll",
  "done",
};

static struct {
  bool enabled, started, finished, have_pressure, have_coordinates, have_width;
  bool have_spacing;
  unsigned window;
  uint64_t started_at, reported_at, previous_at;
  uint64_t samples, accepted, resets, rejections[TOUCHPAD_REJECT_COUNT_TOTAL];
  uint64_t contacts[4], pressure[5], width[4], spacing_min, spacing_max;
  uint8_t pressure_min, pressure_max, width_min, width_max;
  int32_t x_min, x_max, y_min, y_max;
} diagnostic;

static void clear_baseline(void)
{
  baseline = false;
  stable_samples = 0;
  motion_fraction_x = motion_fraction_y = 0;
  scroll_pending_x = scroll_pending_y = 0;
  scroll_active_x = scroll_active_y = false;
}

void touchpad_reset(void)
{
  loss_quarantine = true;
  clear_baseline();
  if (diagnostic.enabled && !diagnostic.finished) {
    diagnostic.resets++;
  }
}

void touchpad_init(const struct mouse_profile *profile)
{
  ready = profile && profile->absolute && profile->x_resolution &&
      profile->y_resolution && profile->x_max > profile->x_min &&
      profile->y_max > profile->y_min;
  if (ready) {
    geometry = *profile;
  }
  diagnostic = (typeof(diagnostic)) {
    .enabled = ready && boot_options_get()->ps2_synaptics_diagnose,
  };
  present = admitted = palm = typing_quarantine = false;
  typing_blocked = loss_quarantine = false;
  fingers = 0;
  origin_edges = 0;
  origin_known = false;
  seen_typing_ns = 0;
  clear_baseline();
}

static void reject(enum touchpad_rejection reason,
    struct touchpad_motion *motion)
{
  clear_baseline();
  motion->reset = true;
  if (diagnostic.enabled && !diagnostic.finished) {
    diagnostic.rejections[reason]++;
  }
}

static unsigned edge_origin(int32_t x, int32_t y)
{
  int64_t band_x = (int64_t)geometry.x_resolution * TOUCHPAD_EDGE_MM;
  int64_t band_y = (int64_t)geometry.y_resolution * TOUCHPAD_EDGE_MM;
  unsigned edges = 0;
  if (x < (int64_t)geometry.x_min + band_x) {
    edges |= TOUCHPAD_EDGE_LEFT;
  }
  if (x > (int64_t)geometry.x_max - band_x) {
    edges |= TOUCHPAD_EDGE_RIGHT;
  }
  if (y < (int64_t)geometry.y_min + band_y) {
    edges |= TOUCHPAD_EDGE_BOTTOM;
  }
  if (y > (int64_t)geometry.y_max - band_y) {
    edges |= TOUCHPAD_EDGE_TOP;
  }
  return edges;
}

static bool entered_inward(int32_t x, int32_t y)
{
  int64_t inward_x = (int64_t)geometry.x_resolution * TOUCHPAD_INWARD_MM;
  int64_t inward_y = (int64_t)geometry.y_resolution * TOUCHPAD_INWARD_MM;
  if (edge_origin(x, y)) {
    return false;
  }
  if ((origin_edges & TOUCHPAD_EDGE_LEFT) && x < (int64_t)origin_x + inward_x) {
    return false;
  }
  if ((origin_edges & TOUCHPAD_EDGE_RIGHT) && x > (int64_t)origin_x - inward_x) {
    return false;
  }
  if ((origin_edges & TOUCHPAD_EDGE_BOTTOM) && y < (int64_t)origin_y + inward_y) {
    return false;
  }
  if ((origin_edges & TOUCHPAD_EDGE_TOP) && y > (int64_t)origin_y - inward_y) {
    return false;
  }
  return true;
}

static int32_t movement(int64_t delta, uint32_t resolution, int64_t *fraction)
{
  int64_t scaled = delta * TOUCHPAD_PIXELS_PER_MM + *fraction;
  int64_t pixels = scaled / resolution;
  *fraction = scaled % resolution;
  return (int32_t)pixels;
}

static int32_t scroll(int64_t delta, uint32_t resolution,
    int64_t *pending, bool *active)
{
  if (*active) {
    return (int32_t)delta;
  }
  *pending += delta;
  int64_t dead_zone = ((int64_t)resolution + 1) / 2;
  if (*pending > dead_zone) {
    delta = *pending - dead_zone;
  } else if (*pending < -dead_zone) {
    delta = *pending + dead_zone;
  } else {
    return 0;
  }
  *active = true;
  *pending = 0;
  return (int32_t)delta;
}

static void observe(const struct touchpad_sample *sample, uint64_t now)
{
  if (!diagnostic.enabled || diagnostic.finished) {
    return;
  }
  diagnostic.samples++;
  if (diagnostic.previous_at && now >= diagnostic.previous_at) {
    uint64_t spacing = now - diagnostic.previous_at;
    if (!diagnostic.have_spacing || spacing < diagnostic.spacing_min) {
      diagnostic.spacing_min = spacing;
    }
    if (spacing > diagnostic.spacing_max) {
      diagnostic.spacing_max = spacing;
    }
    diagnostic.have_spacing = true;
  }
  diagnostic.previous_at = now;
  if (!sample->valid) {
    return;
  }
  diagnostic.contacts[sample->fingers <= 2 ? sample->fingers : 3]++;
  unsigned pressure_bin = sample->pressure < TOUCHPAD_PRESSURE_OFF ? 0 :
      sample->pressure < TOUCHPAD_PRESSURE_ON ? 1 : sample->pressure < 100 ? 2 :
      sample->pressure < TOUCHPAD_PALM_PRESSURE ? 3 : 4;
  diagnostic.pressure[pressure_bin]++;
  if (!diagnostic.have_pressure || sample->pressure < diagnostic.pressure_min) {
    diagnostic.pressure_min = sample->pressure;
  }
  if (!diagnostic.have_pressure || sample->pressure > diagnostic.pressure_max) {
    diagnostic.pressure_max = sample->pressure;
  }
  diagnostic.have_pressure = true;
  /* Lift packets do not supply contact coordinates. */
  if (sample->pressure > TOUCHPAD_PRESSURE_OFF &&
      sample->x >= geometry.x_min && sample->x <= geometry.x_max &&
      sample->y >= geometry.y_min && sample->y <= geometry.y_max) {
    if (!diagnostic.have_coordinates || sample->x < diagnostic.x_min) {
      diagnostic.x_min = sample->x;
    }
    if (!diagnostic.have_coordinates || sample->x > diagnostic.x_max) {
      diagnostic.x_max = sample->x;
    }
    if (!diagnostic.have_coordinates || sample->y < diagnostic.y_min) {
      diagnostic.y_min = sample->y;
    }
    if (!diagnostic.have_coordinates || sample->y > diagnostic.y_max) {
      diagnostic.y_max = sample->y;
    }
    diagnostic.have_coordinates = true;
  }
  if (sample->width_valid) {
    unsigned width_bin = sample->width < 4 ? 0 : sample->width < 8 ? 1 :
        sample->width < TOUCHPAD_PALM_WIDTH ? 2 : 3;
    diagnostic.width[width_bin]++;
    if (!diagnostic.have_width || sample->width < diagnostic.width_min) {
      diagnostic.width_min = sample->width;
    }
    if (!diagnostic.have_width || sample->width > diagnostic.width_max) {
      diagnostic.width_max = sample->width;
    }
    diagnostic.have_width = true;
  }
}

void touchpad_report(const struct touchpad_sample *sample,
    uint64_t last_typing_ns, uint64_t now, struct touchpad_motion *motion)
{
  *motion = (struct touchpad_motion) {0};
  observe(sample, now);
  if (!ready || !sample->valid) {
    reject(TOUCHPAD_REJECT_INVALID, motion);
    return;
  }
  if (sample->pressure <= TOUCHPAD_PRESSURE_OFF) {
    present = admitted = palm = typing_quarantine = false;
    loss_quarantine = false;
    fingers = 0;
    origin_edges = 0;
    origin_known = false;
    typing_blocked = false;
    reject(TOUCHPAD_REJECT_LIFT, motion);
    return;
  }
  bool coordinates_valid = sample->x >= geometry.x_min &&
      sample->x <= geometry.x_max && sample->y >= geometry.y_min &&
      sample->y <= geometry.y_max;
  bool blocked = last_typing_ns &&
      (now < last_typing_ns || now - last_typing_ns < TOUCHPAD_TYPING_NS);
  /* There need not have been a touch packet during the typing interval. */
  if (last_typing_ns != seen_typing_ns) {
    seen_typing_ns = last_typing_ns;
    clear_baseline();
    motion->reset = true;
  }
  bool new_contact = !present;
  bool changed_count = fingers != sample->fingers;
  if ((new_contact || changed_count) && blocked) {
    typing_quarantine = true;
  }
  present = true;
  if (new_contact || changed_count) {
    fingers = sample->fingers;
    if (new_contact || !origin_edges) {
      origin_known = false;
    }
    clear_baseline();
    motion->reset = true;
  }
  if (sample->pressure >= TOUCHPAD_PALM_PRESSURE ||
      (sample->width_valid && sample->width >= TOUCHPAD_PALM_WIDTH)) {
    palm = true;
  }
  if (sample->pressure >= TOUCHPAD_PRESSURE_ON) {
    admitted = true;
  }
  /* Invalid coordinates cannot erase qualified contact/rejection facts or
   * supply an origin. The first usable sample still establishes edge policy. */
  if (!coordinates_valid) {
    reject(TOUCHPAD_REJECT_INVALID, motion);
    return;
  }
  if (!origin_known) {
    origin_x = sample->x;
    origin_y = sample->y;
    origin_edges = edge_origin(sample->x, sample->y);
    origin_known = true;
    clear_baseline();
    motion->reset = true;
  }
  if (palm) {
    reject(TOUCHPAD_REJECT_PALM, motion);
    return;
  }
  if (loss_quarantine) {
    reject(TOUCHPAD_REJECT_LOSS, motion);
    return;
  }
  if (blocked || typing_quarantine) {
    typing_blocked = true;
    reject(TOUCHPAD_REJECT_TYPING, motion);
    return;
  }
  if (typing_blocked) {
    clear_baseline();
    motion->reset = true;
    typing_blocked = false;
  }
  if (!admitted) {
    reject(TOUCHPAD_REJECT_PRESSURE, motion);
    return;
  }
  if (fingers != 1 && fingers != 2) {
    reject(TOUCHPAD_REJECT_COUNT, motion);
    return;
  }
  if (origin_edges) {
    if (!entered_inward(sample->x, sample->y)) {
      reject(TOUCHPAD_REJECT_EDGE, motion);
      return;
    }
    origin_edges = 0;
    clear_baseline();
    motion->reset = true;
  }
  if (!baseline) {
    previous_x = sample->x;
    previous_y = sample->y;
    baseline = true;
    stable_samples = 1;
    motion->reset = true;
    return;
  }
  if (stable_samples < TOUCHPAD_STABLE_SAMPLES) {
    stable_samples++;
    if (stable_samples < TOUCHPAD_STABLE_SAMPLES) {
      previous_x = sample->x;
      previous_y = sample->y;
      motion->reset = true;
      return;
    }
  }
  int64_t dx = (int64_t)sample->x - previous_x;
  int64_t dy = (int64_t)previous_y - sample->y;
  previous_x = sample->x;
  previous_y = sample->y;
  if (fingers == 1) {
    motion->dx = movement(dx, geometry.x_resolution, &motion_fraction_x);
    motion->dy = movement(dy, geometry.y_resolution, &motion_fraction_y);
  } else {
    motion->scroll_x = scroll(dx, geometry.x_resolution,
        &scroll_pending_x, &scroll_active_x);
    motion->scroll_y = scroll(dy, geometry.y_resolution,
        &scroll_pending_y, &scroll_active_y);
  }
  if (diagnostic.enabled && !diagnostic.finished) {
    diagnostic.accepted++;
  }
}

static void diagnostic_summary(uint64_t elapsed)
{
  klog("synaptics-contact: t=%lu s window=%s samples=%lu accepted=%lu transport-resets=%lu gap-ns=%lu..%lu\n",
      elapsed / TOUCHPAD_SECOND_NS, diagnostic_windows[diagnostic.window],
      diagnostic.samples, diagnostic.accepted, diagnostic.resets,
      diagnostic.spacing_min, diagnostic.spacing_max);
  klog("synaptics-contact: reject lift/pressure/invalid/count/palm/typing/edge/loss=%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu fingers 0/1/2/other=%lu/%lu/%lu/%lu\n",
      diagnostic.rejections[TOUCHPAD_REJECT_LIFT],
      diagnostic.rejections[TOUCHPAD_REJECT_PRESSURE],
      diagnostic.rejections[TOUCHPAD_REJECT_INVALID],
      diagnostic.rejections[TOUCHPAD_REJECT_COUNT],
      diagnostic.rejections[TOUCHPAD_REJECT_PALM],
      diagnostic.rejections[TOUCHPAD_REJECT_TYPING],
      diagnostic.rejections[TOUCHPAD_REJECT_EDGE],
      diagnostic.rejections[TOUCHPAD_REJECT_LOSS], diagnostic.contacts[0],
      diagnostic.contacts[1], diagnostic.contacts[2], diagnostic.contacts[3]);
  klog("synaptics-contact: Z-valid=%u Z=%u..%u bins <25/<30/<100/<220/rest=%lu/%lu/%lu/%lu/%lu W-valid=%u W=%u..%u bins <4/<8/<12/rest=%lu/%lu/%lu/%lu\n",
      diagnostic.have_pressure, diagnostic.pressure_min, diagnostic.pressure_max,
      diagnostic.pressure[0],
      diagnostic.pressure[1], diagnostic.pressure[2], diagnostic.pressure[3],
      diagnostic.pressure[4], diagnostic.have_width, diagnostic.width_min,
      diagnostic.width_max, diagnostic.width[0], diagnostic.width[1],
      diagnostic.width[2], diagnostic.width[3]);
  klog("synaptics-contact: coords-valid=%u X=%d..%d Y=%d..%d\n",
      diagnostic.have_coordinates, diagnostic.x_min, diagnostic.x_max,
      diagnostic.y_min, diagnostic.y_max);
  diagnostic.samples = diagnostic.accepted = diagnostic.resets = 0;
  for (unsigned i = 0; i < TOUCHPAD_REJECT_COUNT_TOTAL; i++) {
    diagnostic.rejections[i] = 0;
  }
  for (unsigned i = 0; i < 4; i++) {
    diagnostic.contacts[i] = diagnostic.width[i] = 0;
  }
  for (unsigned i = 0; i < 5; i++) {
    diagnostic.pressure[i] = 0;
  }
  diagnostic.spacing_min = diagnostic.spacing_max = 0;
  diagnostic.have_spacing = false;
  diagnostic.have_pressure = false;
  diagnostic.have_coordinates = false;
  diagnostic.have_width = false;
}

void touchpad_service(uint64_t now)
{
  if (!diagnostic.enabled || diagnostic.finished) {
    return;
  }
  if (!diagnostic.started) {
    diagnostic.started = true;
    diagnostic.started_at = diagnostic.reported_at = now;
    klog("synaptics-contact: window=settle duration=10 s; authored tuning Z-on=%u Z-off=%u palm-Z=%u palm-W=%u edge=%u mm inward=%u mm gain=%u px/mm scroll-dead-zone=0.5 mm typing=500 ms\n",
        TOUCHPAD_PRESSURE_ON, TOUCHPAD_PRESSURE_OFF, TOUCHPAD_PALM_PRESSURE,
        TOUCHPAD_PALM_WIDTH, TOUCHPAD_EDGE_MM, TOUCHPAD_INWARD_MM,
        TOUCHPAD_PIXELS_PER_MM);
  }
  if (now < diagnostic.started_at) {
    return;
  }
  uint64_t elapsed = now - diagnostic.started_at;
  unsigned window = 0;
  if (elapsed >= TOUCHPAD_DIAGNOSTIC_SETTLE_NS) {
    uint64_t step = (elapsed - TOUCHPAD_DIAGNOSTIC_SETTLE_NS) /
        TOUCHPAD_DIAGNOSTIC_WINDOW_NS;
    window = step >= 7 ? 8 : (unsigned)step + 1;
  }
  if (window != diagnostic.window) {
    diagnostic_summary(elapsed);
    diagnostic.window = window;
    diagnostic.reported_at = now;
    klog("synaptics-contact: window=%s duration=%u s at=%lu s\n",
        diagnostic_windows[window], window == 8 ? 0 : 20,
        elapsed / TOUCHPAD_SECOND_NS);
    if (window == 8) {
      diagnostic.finished = true;
    }
  } else if (now - diagnostic.reported_at >= TOUCHPAD_DIAGNOSTIC_INTERVAL_NS) {
    diagnostic_summary(elapsed);
    diagnostic.reported_at = now;
  }
}
