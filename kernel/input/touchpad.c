#include <kernel/boot/options.h>
#include <kernel/input/touchpad.h>
#include <kernel/log.h>
#include <kernel/mouse.h>
#include <kernel-config.h>

#define TOUCHPAD_SECOND_NS UINT64_C(1000000000)
#define TOUCHPAD_TYPING_NS (UINT64_C(1000000) * CONFIG_TOUCHPAD_TYPING_MS)
#define TOUCHPAD_PAIR_FRESH_NS (UINT64_C(1000000) * CONFIG_TOUCHPAD_PAIR_FRESH_MS)

_Static_assert(CONFIG_TOUCHPAD_SCROLL_ENTER_MM < CONFIG_TOUCHPAD_SCROLL_LEAVE_MM,
    "scroll leave distance must exceed entry distance");
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
  TOUCHPAD_REJECT_PAIR,
  TOUCHPAD_REJECT_COUNT_TOTAL,
};

struct contact_state {
  int32_t x, y, origin_x, origin_y;
  unsigned origin_edges;
  uint8_t stable_samples;
  bool present, admitted, typing_gated, baseline;
};

struct contact_delta {
  int64_t x, y;
  bool usable;
};

enum touchpad_mode {
  TOUCHPAD_IDLE,
  TOUCHPAD_MOVE,
  TOUCHPAD_SCROLL,
};

static struct mouse_profile geometry;
static struct contact_state contacts[2];
static bool ready, palm, loss_quarantine, scroll_near, previous_pressed, previous_paired;
static uint8_t previous_count, drag_contact;
static enum touchpad_mode mode;
static int64_t motion_fraction_x, motion_fraction_y;
static int64_t scroll_pending_x, scroll_pending_y;
static int64_t centroid_fraction_x, centroid_fraction_y;
static bool scroll_active_x, scroll_active_y;

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
  uint64_t paired, unusable_pairs, ambiguous_pairs, separated_pairs, drag_samples;
  uint64_t basic_scroll_samples;
  uint64_t contacts[4], pressure[5], width[4], spacing_min, spacing_max;
  uint8_t pressure_min, pressure_max, width_min, width_max;
  int32_t x_min, x_max, y_min, y_max;
} diagnostic;

static void clear_fractions(void)
{
  motion_fraction_x = motion_fraction_y = 0;
  scroll_pending_x = scroll_pending_y = 0;
  centroid_fraction_x = centroid_fraction_y = 0;
  scroll_active_x = scroll_active_y = false;
}

static void clear_baseline(void)
{
  for (unsigned i = 0; i < 2; ++i) {
    contacts[i].baseline = false;
    contacts[i].stable_samples = 0;
  }
  scroll_near = false;
  mode = TOUCHPAD_IDLE;
  clear_fractions();
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
  contacts[0] = contacts[1] = (struct contact_state){0};
  palm = loss_quarantine = previous_pressed = previous_paired = false;
  previous_count = drag_contact = 0;
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

static bool entered_inward(const struct contact_state *contact, int32_t x, int32_t y)
{
  int64_t inward_x = (int64_t)geometry.x_resolution * TOUCHPAD_INWARD_MM;
  int64_t inward_y = (int64_t)geometry.y_resolution * TOUCHPAD_INWARD_MM;
  if (edge_origin(x, y)) {
    return false;
  }
  if ((contact->origin_edges & TOUCHPAD_EDGE_LEFT) && x < (int64_t)contact->origin_x + inward_x) {
    return false;
  }
  if ((contact->origin_edges & TOUCHPAD_EDGE_RIGHT) && x > (int64_t)contact->origin_x - inward_x) {
    return false;
  }
  if ((contact->origin_edges & TOUCHPAD_EDGE_BOTTOM) && y < (int64_t)contact->origin_y + inward_y) {
    return false;
  }
  if ((contact->origin_edges & TOUCHPAD_EDGE_TOP) && y > (int64_t)contact->origin_y - inward_y) {
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

static bool coordinates_valid(int32_t x, int32_t y)
{
  return x >= geometry.x_min && x <= geometry.x_max &&
      y >= geometry.y_min && y <= geometry.y_max;
}

static bool typing_active(uint64_t last_typing_ns, uint64_t now)
{
  return last_typing_ns && (now < last_typing_ns ||
      now - last_typing_ns < TOUCHPAD_TYPING_NS);
}

static uint64_t physical_distance(int64_t dx, int64_t dy)
{
  int64_t x = dx * geometry.y_resolution;
  int64_t y = dy * geometry.x_resolution;
  return (uint64_t)(x * x + y * y);
}

static uint64_t separation_limit(unsigned mm)
{
  uint64_t scaled = (uint64_t)mm * geometry.x_resolution * geometry.y_resolution;
  return scaled * scaled;
}

static uint64_t contact_distance(unsigned contact, int32_t x, int32_t y)
{
  return physical_distance((int64_t)x - contacts[contact].x,
      (int64_t)y - contacts[contact].y);
}

static bool match_contacts(const int32_t *x, const int32_t *y,
    unsigned count, unsigned *mapping)
{
  unsigned known = contacts[0].present + contacts[1].present;
  mapping[0] = 0;
  mapping[1] = 1;
  if (!known) {
    return true;
  }
  if (known == 1) {
    unsigned existing = contacts[0].present ? 0 : 1;
    if (count == 1) {
      mapping[0] = existing;
      return true;
    }
    uint64_t first = contact_distance(existing, x[0], y[0]);
    uint64_t second = contact_distance(existing, x[1], y[1]);
    if (first == second) {
      return false;
    }
    unsigned nearest = first < second ? 0 : 1;
    mapping[nearest] = existing;
    mapping[1 - nearest] = 1 - existing;
    return true;
  }
  uint64_t first = contact_distance(0, x[0], y[0]);
  uint64_t second = contact_distance(1, x[0], y[0]);
  if (count == 2) {
    first += contact_distance(1, x[1], y[1]);
    second += contact_distance(0, x[1], y[1]);
  }
  if (first == second) {
    return false;
  }
  if (second < first) {
    mapping[0] = 1;
    mapping[1] = 0;
  }
  return true;
}

static void contact_rejected(enum touchpad_rejection reason)
{
  if (diagnostic.enabled && !diagnostic.finished) {
    diagnostic.rejections[reason]++;
  }
}

static struct contact_delta update_contact(unsigned index, int32_t x, int32_t y,
    uint8_t pressure, uint64_t last_typing_ns, uint64_t now,
    struct touchpad_motion *motion)
{
  struct contact_state *contact = &contacts[index];
  struct contact_delta delta = {0};
  if (!contact->present) {
    *contact = (struct contact_state){
      .present = true,
      .x = x, .y = y, .origin_x = x, .origin_y = y,
      .origin_edges = edge_origin(x, y),
      .typing_gated = typing_active(last_typing_ns, now),
    };
  }
  delta.x = (int64_t)x - contact->x;
  delta.y = (int64_t)contact->y - y;
  contact->x = x;
  contact->y = y;
  if (pressure >= TOUCHPAD_PRESSURE_ON) {
    contact->admitted = true;
  }
  if (contact->typing_gated) {
    if (typing_active(last_typing_ns, now)) {
      contact->baseline = false;
      contact->stable_samples = 0;
      contact_rejected(TOUCHPAD_REJECT_TYPING);
      return (struct contact_delta){0};
    }
    contact->typing_gated = false;
    contact->baseline = false;
    contact->stable_samples = 0;
    motion->reset = true;
  }
  if (!contact->admitted) {
    contact_rejected(TOUCHPAD_REJECT_PRESSURE);
    contact->baseline = false;
    contact->stable_samples = 0;
    return (struct contact_delta){0};
  }
  if (contact->origin_edges) {
    if (!entered_inward(contact, x, y)) {
      contact_rejected(TOUCHPAD_REJECT_EDGE);
      contact->baseline = false;
      contact->stable_samples = 0;
      return (struct contact_delta){0};
    }
    contact->origin_edges = 0;
    contact->baseline = false;
    contact->stable_samples = 0;
    motion->reset = true;
  }
  if (!contact->baseline) {
    contact->baseline = true;
    contact->stable_samples = 1;
    return (struct contact_delta){0};
  }
  if (contact->stable_samples < TOUCHPAD_STABLE_SAMPLES) {
    contact->stable_samples++;
  }
  delta.usable = contact->stable_samples >= TOUCHPAD_STABLE_SAMPLES;
  return delta;
}

void touchpad_report(const struct touchpad_sample *sample,
    unsigned buttons, uint64_t last_typing_ns, uint64_t now,
    struct touchpad_motion *motion)
{
  *motion = (struct touchpad_motion){0};
  observe(sample, now);
  if (!ready || !sample->valid) {
    reject(TOUCHPAD_REJECT_INVALID, motion);
    return;
  }
  if (sample->pressure <= TOUCHPAD_PRESSURE_OFF) {
    /* A weak primary with multiple contact hints is not a confirmed lift
     * of the other finger; retain admission and rejection lifetimes. */
    if (geometry.has_second && sample->fingers >= 2) {
      if (diagnostic.enabled && !diagnostic.finished) {
        diagnostic.unusable_pairs++;
      }
      reject(TOUCHPAD_REJECT_PAIR, motion);
      return;
    }
    contacts[0] = contacts[1] = (struct contact_state){0};
    palm = loss_quarantine = previous_pressed = previous_paired = false;
    previous_count = 0;
    reject(TOUCHPAD_REJECT_LIFT, motion);
    return;
  }
  if (sample->pressure >= TOUCHPAD_PALM_PRESSURE ||
      (sample->second_valid && sample->second_pressure >= TOUCHPAD_PALM_PRESSURE) ||
      (sample->width_valid && sample->width >= TOUCHPAD_PALM_WIDTH)) {
    palm = true;
  }
  if (palm) {
    reject(TOUCHPAD_REJECT_PALM, motion);
    return;
  }
  if (loss_quarantine) {
    reject(TOUCHPAD_REJECT_LOSS, motion);
    return;
  }
  if (sample->fingers != 1 && sample->fingers != 2) {
    reject(TOUCHPAD_REJECT_COUNT, motion);
    return;
  }
  if (!coordinates_valid(sample->x, sample->y)) {
    reject(TOUCHPAD_REJECT_INVALID, motion);
    return;
  }
  bool paired = sample->fingers == 2 && geometry.has_second &&
      sample->second_valid && coordinates_valid(sample->second_x, sample->second_y) &&
      sample->second_pressure > TOUCHPAD_PRESSURE_OFF &&
      now >= sample->observed_at && now >= sample->second_at &&
      now - sample->observed_at <= TOUCHPAD_PAIR_FRESH_NS &&
      now - sample->second_at <= TOUCHPAD_PAIR_FRESH_NS;
  unsigned count = paired ? 2 : 1;
  if (sample->fingers == 2 && geometry.has_second) {
    if (!paired) {
      if (diagnostic.enabled && !diagnostic.finished) {
        diagnostic.unusable_pairs++;
      }
    } else if (diagnostic.enabled && !diagnostic.finished) {
      diagnostic.paired++;
    }
  }
  bool pressed = buttons & MOUSE_BUTTON_LEFT;
  if (previous_count != sample->fingers || previous_pressed != pressed ||
      previous_paired != paired) {
    clear_baseline();
    motion->reset = true;
  }
  previous_count = sample->fingers;
  previous_pressed = pressed;
  previous_paired = paired;
  int32_t x[2] = {sample->x, sample->second_x};
  int32_t y[2] = {sample->y, sample->second_y};
  uint8_t pressure[2] = {sample->pressure, sample->second_pressure};
  unsigned mapping[2];
  if (!match_contacts(x, y, count, mapping)) {
    if (diagnostic.enabled && !diagnostic.finished) {
      diagnostic.ambiguous_pairs++;
    }
    reject(TOUCHPAD_REJECT_PAIR, motion);
    return;
  }
  if (count == 1) {
    unsigned other = 1 - mapping[0];
    if (sample->fingers == 2) {
      /* Missing secondary data does not establish that this finger lifted. */
      contacts[other].baseline = false;
      contacts[other].stable_samples = 0;
    } else {
      contacts[other] = (struct contact_state){0};
    }
  }
  struct contact_delta delta[2] = {0};
  for (unsigned i = 0; i < count; ++i) {
    delta[mapping[i]] = update_contact(mapping[i], x[i], y[i], pressure[i],
        last_typing_ns, now, motion);
  }
  bool both = count == 2 && delta[0].usable && delta[1].usable;
  enum touchpad_mode next = TOUCHPAD_IDLE;
  if (pressed || !both) {
    if (delta[0].usable || delta[1].usable) {
      next = TOUCHPAD_MOVE;
    }
  } else {
    uint64_t distance = physical_distance((int64_t)x[0] - x[1],
        (int64_t)y[0] - y[1]);
    if (scroll_near) {
      scroll_near = distance < separation_limit(CONFIG_TOUCHPAD_SCROLL_LEAVE_MM);
    } else {
      scroll_near = distance <= separation_limit(CONFIG_TOUCHPAD_SCROLL_ENTER_MM);
    }
    if (scroll_near) {
      next = TOUCHPAD_SCROLL;
    } else if (diagnostic.enabled && !diagnostic.finished) {
      diagnostic.separated_pairs++;
    }
  }
  /* Without a second position, only the inherited basic-W scroll is possible.
   * Physical click always turns its two-contact hint into a drag instead. */
  if (!paired && sample->fingers == 2 && !pressed && delta[mapping[0]].usable) {
    next = TOUCHPAD_SCROLL;
    if (diagnostic.enabled && !diagnostic.finished) {
      diagnostic.basic_scroll_samples++;
    }
  }
  if (pressed) {
    scroll_near = false;
  }
  if (mode != next || motion->reset) {
    clear_fractions();
    if (next == TOUCHPAD_SCROLL && mode != next) {
      mode = next;
      motion->reset = true;
      return;
    }
    mode = next;
    motion->reset = true;
  }
  if (mode == TOUCHPAD_MOVE) {
    uint64_t first = physical_distance(delta[0].x, delta[0].y);
    uint64_t second = physical_distance(delta[1].x, delta[1].y);
    if (!delta[0].usable || (delta[1].usable && second > first)) {
      drag_contact = 1;
    } else if (!delta[1].usable || first > second) {
      drag_contact = 0;
    }
    motion->dx = movement(delta[drag_contact].x, geometry.x_resolution, &motion_fraction_x);
    motion->dy = movement(delta[drag_contact].y, geometry.y_resolution, &motion_fraction_y);
    if (pressed && diagnostic.enabled && !diagnostic.finished) {
      diagnostic.drag_samples++;
    }
  } else if (mode == TOUCHPAD_SCROLL) {
    int64_t dx = delta[mapping[0]].x;
    int64_t dy = delta[mapping[0]].y;
    if (both) {
      dx += delta[mapping[1]].x + centroid_fraction_x;
      dy += delta[mapping[1]].y + centroid_fraction_y;
      centroid_fraction_x = dx % 2;
      centroid_fraction_y = dy % 2;
      dx /= 2;
      dy /= 2;
    }
    motion->scroll_x = scroll(dx, geometry.x_resolution, &scroll_pending_x, &scroll_active_x);
    motion->scroll_y = scroll(dy, geometry.y_resolution, &scroll_pending_y, &scroll_active_y);
  }
  if (mode != TOUCHPAD_IDLE && diagnostic.enabled && !diagnostic.finished) {
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
  klog("synaptics-contact: pair usable/unusable/ambiguous/separated=%lu/%lu/%lu/%lu drag=%lu basic-W-scroll=%lu\n",
      diagnostic.paired, diagnostic.unusable_pairs, diagnostic.ambiguous_pairs,
      diagnostic.separated_pairs, diagnostic.drag_samples, diagnostic.basic_scroll_samples);
  diagnostic.paired = diagnostic.unusable_pairs = diagnostic.ambiguous_pairs = 0;
  diagnostic.separated_pairs = diagnostic.drag_samples = 0;
  diagnostic.basic_scroll_samples = 0;
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
    klog("synaptics-contact: window=settle duration=10 s; authored tuning Z-on=%u Z-off=%u palm-Z=%u palm-W=%u edge=%u mm inward=%u mm gain=%u px/mm scroll-dead-zone=0.5 mm typing=%u ms pair-fresh=%u ms scroll-enter/leave=%u/%u mm\n",
        TOUCHPAD_PRESSURE_ON, TOUCHPAD_PRESSURE_OFF, TOUCHPAD_PALM_PRESSURE,
        TOUCHPAD_PALM_WIDTH, TOUCHPAD_EDGE_MM, TOUCHPAD_INWARD_MM,
        TOUCHPAD_PIXELS_PER_MM, CONFIG_TOUCHPAD_TYPING_MS,
        CONFIG_TOUCHPAD_PAIR_FRESH_MS, CONFIG_TOUCHPAD_SCROLL_ENTER_MM,
        CONFIG_TOUCHPAD_SCROLL_LEAVE_MM);
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
