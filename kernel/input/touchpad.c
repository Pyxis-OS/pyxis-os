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
/* These are authored initial tuning, pending native qualification. Pressure
 * and basic-W width are device hints, not universal force/contact scales. */
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

struct contact_delta {
  int64_t x, y;
  bool usable;
};

static const char *const diagnostic_windows[] = {
  "settle", "normal-finger", "edge-entry", "resting-palms", "typing",
  "finger-transitions-physical-drag", "TrackPoint-with-palm", "two-finger-scroll",
  "done",
};

static void clear_fractions(struct touchpad_state *state)
{
  state->motion_fraction_x = state->motion_fraction_y = 0;
  state->scroll_pending_x = state->scroll_pending_y = 0;
  state->centroid_fraction_x = state->centroid_fraction_y = 0;
  state->scroll_active_x = state->scroll_active_y = false;
}

static void clear_baseline(struct touchpad_state *state)
{
  for (unsigned i = 0; i < 2; ++i) {
    state->contacts[i].baseline = false;
    state->contacts[i].stable_samples = 0;
  }
  state->scroll_near = false;
  state->mode = TOUCHPAD_IDLE;
  clear_fractions(state);
}

void touchpad_reset(struct touchpad_state *state)
{
  state->loss_quarantine = true;
  clear_baseline(state);
  if (state->diagnostic.enabled && !state->diagnostic.finished) {
    state->diagnostic.resets++;
  }
}

void touchpad_init(struct touchpad_state *state, const struct mouse_profile *profile)
{
  state->ready = profile && profile->absolute && profile->x_resolution &&
      profile->y_resolution && profile->x_max > profile->x_min &&
      profile->y_max > profile->y_min;
  if (state->ready && profile->native_contacts) {
    /* Bound both signed squared-distance terms and their sum before any
     * contact reaches physical_distance(). This is an arithmetic limit. */
    state->ready = profile->x_resolution == TOUCHPAD_NATIVE_COUNTS_PER_MM &&
        profile->y_resolution == TOUCHPAD_NATIVE_COUNTS_PER_MM &&
        (int64_t)profile->x_max - profile->x_min <= INT32_MAX / profile->y_resolution &&
        (int64_t)profile->y_max - profile->y_min <= INT32_MAX / profile->x_resolution;
  }
  if (state->ready) {
    state->geometry = *profile;
  }
  state->diagnostic = (typeof(state->diagnostic)) {
    .enabled = state->ready && !profile->native_contacts &&
        boot_options_get()->ps2_synaptics_diagnose,
  };
  state->contacts[0] = state->contacts[1] = (struct touchpad_contact_state){0};
  state->palm = state->loss_quarantine = state->previous_pressed = state->previous_paired = false;
  state->previous_count = state->drag_contact = 0;
  clear_baseline(state);
}

static void reject(struct touchpad_state *state, enum touchpad_rejection reason,
    struct touchpad_motion *motion)
{
  clear_baseline(state);
  motion->reset = true;
  if (state->diagnostic.enabled && !state->diagnostic.finished) {
    state->diagnostic.rejections[reason]++;
  }
}

static void confirmed_lift(struct touchpad_state *state, struct touchpad_motion *motion)
{
  state->contacts[0] = state->contacts[1] = (struct touchpad_contact_state){0};
  state->palm = state->loss_quarantine = state->previous_pressed = state->previous_paired = false;
  state->previous_count = 0;
  reject(state, TOUCHPAD_REJECT_LIFT, motion);
}

static unsigned edge_origin(struct touchpad_state *state, int32_t x, int32_t y)
{
  int64_t band_x = (int64_t)state->geometry.x_resolution * TOUCHPAD_EDGE_MM;
  int64_t band_y = (int64_t)state->geometry.y_resolution * TOUCHPAD_EDGE_MM;
  unsigned edges = 0;
  if (x < (int64_t)state->geometry.x_min + band_x) {
    edges |= TOUCHPAD_EDGE_LEFT;
  }
  if (x > (int64_t)state->geometry.x_max - band_x) {
    edges |= TOUCHPAD_EDGE_RIGHT;
  }
  if (y < (int64_t)state->geometry.y_min + band_y) {
    edges |= TOUCHPAD_EDGE_BOTTOM;
  }
  if (y > (int64_t)state->geometry.y_max - band_y) {
    edges |= TOUCHPAD_EDGE_TOP;
  }
  return edges;
}

static bool entered_inward(struct touchpad_state *state,
    const struct touchpad_contact_state *contact, int32_t x, int32_t y)
{
  int64_t inward_x = (int64_t)state->geometry.x_resolution * TOUCHPAD_INWARD_MM;
  int64_t inward_y = (int64_t)state->geometry.y_resolution * TOUCHPAD_INWARD_MM;
  if (edge_origin(state, x, y)) {
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

static void observe(struct touchpad_state *state,
    const struct touchpad_sample *sample, uint64_t now)
{
  if (!state->diagnostic.enabled || state->diagnostic.finished) {
    return;
  }
  state->diagnostic.samples++;
  if (state->diagnostic.previous_at && now >= state->diagnostic.previous_at) {
    uint64_t spacing = now - state->diagnostic.previous_at;
    if (!state->diagnostic.have_spacing || spacing < state->diagnostic.spacing_min) {
      state->diagnostic.spacing_min = spacing;
    }
    if (spacing > state->diagnostic.spacing_max) {
      state->diagnostic.spacing_max = spacing;
    }
    state->diagnostic.have_spacing = true;
  }
  state->diagnostic.previous_at = now;
  if (!sample->valid) {
    return;
  }
  state->diagnostic.contacts[sample->fingers <= 2 ? sample->fingers : 3]++;
  unsigned pressure_bin = sample->pressure < TOUCHPAD_PRESSURE_OFF ? 0 :
      sample->pressure < TOUCHPAD_PRESSURE_ON ? 1 : sample->pressure < 100 ? 2 :
      sample->pressure < TOUCHPAD_PALM_PRESSURE ? 3 : 4;
  state->diagnostic.pressure[pressure_bin]++;
  if (!state->diagnostic.have_pressure || sample->pressure < state->diagnostic.pressure_min) {
    state->diagnostic.pressure_min = sample->pressure;
  }
  if (!state->diagnostic.have_pressure || sample->pressure > state->diagnostic.pressure_max) {
    state->diagnostic.pressure_max = sample->pressure;
  }
  state->diagnostic.have_pressure = true;
  /* Lift packets do not supply contact coordinates. */
  if (sample->pressure > TOUCHPAD_PRESSURE_OFF &&
      sample->x >= state->geometry.x_min && sample->x <= state->geometry.x_max &&
      sample->y >= state->geometry.y_min && sample->y <= state->geometry.y_max) {
    if (!state->diagnostic.have_coordinates || sample->x < state->diagnostic.x_min) {
      state->diagnostic.x_min = sample->x;
    }
    if (!state->diagnostic.have_coordinates || sample->x > state->diagnostic.x_max) {
      state->diagnostic.x_max = sample->x;
    }
    if (!state->diagnostic.have_coordinates || sample->y < state->diagnostic.y_min) {
      state->diagnostic.y_min = sample->y;
    }
    if (!state->diagnostic.have_coordinates || sample->y > state->diagnostic.y_max) {
      state->diagnostic.y_max = sample->y;
    }
    state->diagnostic.have_coordinates = true;
  }
  if (sample->width_valid) {
    unsigned width_bin = sample->width < 4 ? 0 : sample->width < 8 ? 1 :
        sample->width < TOUCHPAD_PALM_WIDTH ? 2 : 3;
    state->diagnostic.width[width_bin]++;
    if (!state->diagnostic.have_width || sample->width < state->diagnostic.width_min) {
      state->diagnostic.width_min = sample->width;
    }
    if (!state->diagnostic.have_width || sample->width > state->diagnostic.width_max) {
      state->diagnostic.width_max = sample->width;
    }
    state->diagnostic.have_width = true;
  }
}

static bool coordinates_valid(struct touchpad_state *state, int32_t x, int32_t y)
{
  return x >= state->geometry.x_min && x <= state->geometry.x_max &&
      y >= state->geometry.y_min && y <= state->geometry.y_max;
}

static bool typing_active(uint64_t last_typing_ns, uint64_t now)
{
  return last_typing_ns && (now < last_typing_ns ||
      now - last_typing_ns < TOUCHPAD_TYPING_NS);
}

static uint64_t physical_distance(struct touchpad_state *state, int64_t dx, int64_t dy)
{
  int64_t x = dx * state->geometry.y_resolution;
  int64_t y = dy * state->geometry.x_resolution;
  return (uint64_t)(x * x + y * y);
}

static uint64_t separation_limit(struct touchpad_state *state, unsigned mm)
{
  uint64_t scaled = (uint64_t)mm * state->geometry.x_resolution * state->geometry.y_resolution;
  return scaled * scaled;
}

static uint64_t contact_distance(struct touchpad_state *state,
    unsigned contact, int32_t x, int32_t y)
{
  return physical_distance(state, (int64_t)x - state->contacts[contact].x,
      (int64_t)y - state->contacts[contact].y);
}

static bool match_contacts(struct touchpad_state *state, const int32_t *x, const int32_t *y,
    unsigned count, unsigned *mapping)
{
  unsigned known = state->contacts[0].present + state->contacts[1].present;
  mapping[0] = 0;
  mapping[1] = 1;
  if (!known) {
    return true;
  }
  if (known == 1) {
    unsigned existing = state->contacts[0].present ? 0 : 1;
    if (count == 1) {
      mapping[0] = existing;
      return true;
    }
    uint64_t first = contact_distance(state, existing, x[0], y[0]);
    uint64_t second = contact_distance(state, existing, x[1], y[1]);
    if (first == second) {
      return false;
    }
    unsigned nearest = first < second ? 0 : 1;
    mapping[nearest] = existing;
    mapping[1 - nearest] = 1 - existing;
    return true;
  }
  uint64_t first = contact_distance(state, 0, x[0], y[0]);
  uint64_t second = contact_distance(state, 1, x[0], y[0]);
  if (count == 2) {
    first += contact_distance(state, 1, x[1], y[1]);
    second += contact_distance(state, 0, x[1], y[1]);
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

static bool native_frame(struct touchpad_state *state, const struct touchpad_sample *sample,
    uint64_t now, int32_t *x, int32_t *y, uint32_t *ids, unsigned *count,
    struct touchpad_motion *motion)
{
  if (!sample->contacts_complete || (sample->contact_count && !sample->contacts) ||
      now < sample->observed_at || now - sample->observed_at > TOUCHPAD_PAIR_FRESH_NS) {
    touchpad_reset(state);
    reject(state, TOUCHPAD_REJECT_INVALID, motion);
    return false;
  }
  unsigned active = 0, tips = 0;
  bool invalid = false;
  for (unsigned i = 0; i < sample->contact_count; ++i) {
    const struct touchpad_contact *contact = &sample->contacts[i];
    if (contact->flags & ~(TOUCHPAD_CONTACT_TIP | TOUCHPAD_CONTACT_CONFIDENCE |
        TOUCHPAD_CONTACT_ID_VALID | TOUCHPAD_CONTACT_BUTTON_ZONE)) {
      invalid = true;
    }
    if (!(contact->flags & TOUCHPAD_CONTACT_TIP)) {
      continue;
    }
    ++tips;
    if (!(contact->flags & TOUCHPAD_CONTACT_CONFIDENCE)) {
      state->palm = true;
    }
    if (!(contact->flags & TOUCHPAD_CONTACT_ID_VALID)) {
      invalid = true;
    }
    if (contact->flags & TOUCHPAD_CONTACT_BUTTON_ZONE) {
      continue;
    }
    if (active < 2) {
      x[active] = contact->x;
      y[active] = contact->y;
      ids[active] = contact->id;
    }
    ++active;
  }
  if (invalid) {
    touchpad_reset(state);
    reject(state, TOUCHPAD_REJECT_INVALID, motion);
    return false;
  }
  if (!tips) {
    confirmed_lift(state, motion);
    return false;
  }
  if (state->palm) {
    reject(state, TOUCHPAD_REJECT_PALM, motion);
    return false;
  }
  if (state->loss_quarantine) {
    reject(state, TOUCHPAD_REJECT_LOSS, motion);
    return false;
  }
  if (!active) {
    reject(state, TOUCHPAD_REJECT_EDGE, motion);
    return false;
  }
  if (active > 2) {
    state->contacts[0] = state->contacts[1] = (struct touchpad_contact_state){0};
    reject(state, TOUCHPAD_REJECT_COUNT, motion);
    return false;
  }
  if ((active == 2 && ids[0] == ids[1]) ||
      !coordinates_valid(state, x[0], y[0]) ||
      (active == 2 && !coordinates_valid(state, x[1], y[1]))) {
    touchpad_reset(state);
    reject(state, TOUCHPAD_REJECT_INVALID, motion);
    return false;
  }
  *count = active;
  return true;
}

static void match_identified_contacts(struct touchpad_state *state, const uint32_t *ids,
    unsigned count, unsigned *mapping, struct touchpad_motion *motion)
{
  bool used[2] = {false, false};
  bool changed = false;
  for (unsigned slot = 0; slot < 2; ++slot) {
    bool present = false;
    for (unsigned i = 0; i < count; ++i) {
      present |= state->contacts[slot].present && state->contacts[slot].id == ids[i];
    }
    if (state->contacts[slot].present && !present) {
      state->contacts[slot] = (struct touchpad_contact_state){0};
      changed = true;
    }
  }
  for (unsigned i = 0; i < count; ++i) {
    mapping[i] = 2;
    for (unsigned slot = 0; slot < 2; ++slot) {
      if (state->contacts[slot].present && state->contacts[slot].id == ids[i]) {
        mapping[i] = slot;
        used[slot] = true;
      }
    }
  }
  for (unsigned i = 0; i < count; ++i) {
    if (mapping[i] != 2) {
      continue;
    }
    unsigned slot = used[0] ? 1 : 0;
    mapping[i] = slot;
    used[slot] = true;
    changed = true;
  }
  if (changed) {
    clear_baseline(state);
    motion->reset = true;
  }
}

static void contact_rejected(struct touchpad_state *state, enum touchpad_rejection reason)
{
  if (state->diagnostic.enabled && !state->diagnostic.finished) {
    state->diagnostic.rejections[reason]++;
  }
}

static struct contact_delta update_contact(struct touchpad_state *state,
    unsigned index, int32_t x, int32_t y,
    bool admitted, uint32_t id, uint64_t last_typing_ns, uint64_t now,
    struct touchpad_motion *motion)
{
  struct touchpad_contact_state *contact = &state->contacts[index];
  struct contact_delta delta = {0};
  if (!contact->present) {
    *contact = (struct touchpad_contact_state){
      .present = true,
      .id = id,
      .x = x, .y = y, .origin_x = x, .origin_y = y,
      .origin_edges = edge_origin(state, x, y),
      .typing_gated = typing_active(last_typing_ns, now),
    };
  }
  delta.x = (int64_t)x - contact->x;
  delta.y = (int64_t)contact->y - y;
  contact->x = x;
  contact->y = y;
  if (admitted) {
    contact->admitted = true;
  }
  if (contact->typing_gated) {
    if (typing_active(last_typing_ns, now)) {
      contact->baseline = false;
      contact->stable_samples = 0;
      contact_rejected(state, TOUCHPAD_REJECT_TYPING);
      return (struct contact_delta){0};
    }
    contact->typing_gated = false;
    contact->baseline = false;
    contact->stable_samples = 0;
    motion->reset = true;
  }
  if (!contact->admitted) {
    contact_rejected(state, TOUCHPAD_REJECT_PRESSURE);
    contact->baseline = false;
    contact->stable_samples = 0;
    return (struct contact_delta){0};
  }
  if (contact->origin_edges) {
    if (!entered_inward(state, contact, x, y)) {
      contact_rejected(state, TOUCHPAD_REJECT_EDGE);
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

void touchpad_report(struct touchpad_state *state, const struct touchpad_sample *sample,
    unsigned buttons, uint64_t last_typing_ns, uint64_t now,
    struct touchpad_motion *motion)
{
  *motion = (struct touchpad_motion){0};
  observe(state, sample, now);
  if (!state->ready || !sample->valid) {
    if (state->geometry.native_contacts) {
      touchpad_reset(state);
    }
    reject(state, TOUCHPAD_REJECT_INVALID, motion);
    return;
  }
  bool native = state->geometry.native_contacts;
  int32_t x[2] = {0}, y[2] = {0};
  uint32_t ids[2] = {0};
  bool admitted[2] = {true, true};
  unsigned count;
  bool paired;
  if (native) {
    if (!native_frame(state, sample, now, x, y, ids, &count, motion)) {
      return;
    }
    paired = count == 2;
  } else {
    if (sample->pressure <= TOUCHPAD_PRESSURE_OFF) {
      /* A weak primary with multiple contact hints is not a confirmed lift
       * of the other finger; retain admission and rejection lifetimes. */
      if (state->geometry.has_second && sample->fingers >= 2) {
        if (state->diagnostic.enabled && !state->diagnostic.finished) {
          state->diagnostic.unusable_pairs++;
        }
        reject(state, TOUCHPAD_REJECT_PAIR, motion);
        return;
      }
      confirmed_lift(state, motion);
      return;
    }
    if (sample->pressure >= TOUCHPAD_PALM_PRESSURE ||
        (sample->second_valid && sample->second_pressure >= TOUCHPAD_PALM_PRESSURE) ||
        (sample->width_valid && sample->width >= TOUCHPAD_PALM_WIDTH)) {
      state->palm = true;
    }
    if (state->palm) {
      reject(state, TOUCHPAD_REJECT_PALM, motion);
      return;
    }
    if (state->loss_quarantine) {
      reject(state, TOUCHPAD_REJECT_LOSS, motion);
      return;
    }
    if (sample->fingers != 1 && sample->fingers != 2) {
      reject(state, TOUCHPAD_REJECT_COUNT, motion);
      return;
    }
    if (!coordinates_valid(state, sample->x, sample->y)) {
      reject(state, TOUCHPAD_REJECT_INVALID, motion);
      return;
    }
    paired = sample->fingers == 2 && state->geometry.has_second &&
        sample->second_valid && coordinates_valid(state, sample->second_x, sample->second_y) &&
        sample->second_pressure > TOUCHPAD_PRESSURE_OFF &&
        now >= sample->observed_at && now >= sample->second_at &&
        now - sample->observed_at <= TOUCHPAD_PAIR_FRESH_NS &&
        now - sample->second_at <= TOUCHPAD_PAIR_FRESH_NS;
    count = paired ? 2 : 1;
    if (sample->fingers == 2 && state->geometry.has_second) {
      if (!paired) {
        if (state->diagnostic.enabled && !state->diagnostic.finished) {
          state->diagnostic.unusable_pairs++;
        }
      } else if (state->diagnostic.enabled && !state->diagnostic.finished) {
        state->diagnostic.paired++;
      }
    }
    x[0] = sample->x;
    x[1] = sample->second_x;
    y[0] = sample->y;
    y[1] = sample->second_y;
    admitted[0] = sample->pressure >= TOUCHPAD_PRESSURE_ON;
    admitted[1] = sample->second_pressure >= TOUCHPAD_PRESSURE_ON;
  }
  unsigned fingers = native ? count : sample->fingers;
  bool pressed = buttons & MOUSE_BUTTON_LEFT;
  if (state->previous_count != fingers || state->previous_pressed != pressed ||
      state->previous_paired != paired) {
    clear_baseline(state);
    motion->reset = true;
  }
  state->previous_count = fingers;
  state->previous_pressed = pressed;
  state->previous_paired = paired;
  unsigned mapping[2];
  if (native) {
    match_identified_contacts(state, ids, count, mapping, motion);
  } else if (!match_contacts(state, x, y, count, mapping)) {
    if (state->diagnostic.enabled && !state->diagnostic.finished) {
      state->diagnostic.ambiguous_pairs++;
    }
    reject(state, TOUCHPAD_REJECT_PAIR, motion);
    return;
  }
  if (count == 1) {
    unsigned other = 1 - mapping[0];
    if (fingers == 2) {
      /* Missing secondary data does not establish that this finger lifted. */
      state->contacts[other].baseline = false;
      state->contacts[other].stable_samples = 0;
    } else {
      state->contacts[other] = (struct touchpad_contact_state){0};
    }
  }
  struct contact_delta delta[2] = {0};
  for (unsigned i = 0; i < count; ++i) {
    delta[mapping[i]] = update_contact(state, mapping[i], x[i], y[i], admitted[i], ids[i],
        last_typing_ns, now, motion);
  }
  bool both = count == 2 && delta[0].usable && delta[1].usable;
  enum touchpad_mode next = TOUCHPAD_IDLE;
  if (pressed || !both) {
    if (delta[0].usable || delta[1].usable) {
      next = TOUCHPAD_MOVE;
    }
  } else {
    uint64_t distance = physical_distance(state, (int64_t)x[0] - x[1],
        (int64_t)y[0] - y[1]);
    if (state->scroll_near) {
      state->scroll_near = distance < separation_limit(state, CONFIG_TOUCHPAD_SCROLL_LEAVE_MM);
    } else {
      state->scroll_near = distance <= separation_limit(state, CONFIG_TOUCHPAD_SCROLL_ENTER_MM);
    }
    if (state->scroll_near) {
      next = TOUCHPAD_SCROLL;
    } else if (state->diagnostic.enabled && !state->diagnostic.finished) {
      state->diagnostic.separated_pairs++;
    }
  }
  /* Without a second position, only the inherited basic-W scroll is possible.
   * Physical click always turns its two-contact hint into a drag instead. */
  if (!native && !paired && fingers == 2 && !pressed && delta[mapping[0]].usable) {
    next = TOUCHPAD_SCROLL;
    if (state->diagnostic.enabled && !state->diagnostic.finished) {
      state->diagnostic.basic_scroll_samples++;
    }
  }
  if (pressed) {
    state->scroll_near = false;
  }
  if (state->mode != next || motion->reset) {
    clear_fractions(state);
    /* Complete ID-based frames already have fresh per-contact baselines. */
    if (!native && next == TOUCHPAD_SCROLL && state->mode != next) {
      state->mode = next;
      motion->reset = true;
      return;
    }
    state->mode = next;
    motion->reset = true;
  }
  if (state->mode == TOUCHPAD_MOVE) {
    uint64_t first = physical_distance(state, delta[0].x, delta[0].y);
    uint64_t second = physical_distance(state, delta[1].x, delta[1].y);
    if (!delta[0].usable || (delta[1].usable && second > first)) {
      state->drag_contact = 1;
    } else if (!delta[1].usable || first > second) {
      state->drag_contact = 0;
    }
    if (native) {
      motion->dx = delta[state->drag_contact].x;
      motion->dy = delta[state->drag_contact].y;
    } else {
      motion->dx = movement(delta[state->drag_contact].x, state->geometry.x_resolution,
          &state->motion_fraction_x);
      motion->dy = movement(delta[state->drag_contact].y, state->geometry.y_resolution,
          &state->motion_fraction_y);
    }
    if (pressed && state->diagnostic.enabled && !state->diagnostic.finished) {
      state->diagnostic.drag_samples++;
    }
  } else if (state->mode == TOUCHPAD_SCROLL) {
    int64_t dx = delta[mapping[0]].x;
    int64_t dy = delta[mapping[0]].y;
    if (both) {
      dx += delta[mapping[1]].x + state->centroid_fraction_x;
      dy += delta[mapping[1]].y + state->centroid_fraction_y;
      state->centroid_fraction_x = dx % 2;
      state->centroid_fraction_y = dy % 2;
      dx /= 2;
      dy /= 2;
    }
    motion->scroll_x = scroll(dx, state->geometry.x_resolution,
        &state->scroll_pending_x, &state->scroll_active_x);
    motion->scroll_y = scroll(dy, state->geometry.y_resolution,
        &state->scroll_pending_y, &state->scroll_active_y);
  }
  if (state->mode != TOUCHPAD_IDLE && state->diagnostic.enabled && !state->diagnostic.finished) {
    state->diagnostic.accepted++;
  }
}

static void diagnostic_summary(struct touchpad_state *state, uint64_t elapsed)
{
  kinfo("synaptics-contact: t=%lu s window=%s samples=%lu accepted=%lu transport-resets=%lu gap-ns=%lu..%lu\n",
      elapsed / TOUCHPAD_SECOND_NS, diagnostic_windows[state->diagnostic.window],
      state->diagnostic.samples, state->diagnostic.accepted, state->diagnostic.resets,
      state->diagnostic.spacing_min, state->diagnostic.spacing_max);
  kinfo("synaptics-contact: reject lift/pressure/invalid/count/palm/typing/edge/loss=%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu fingers 0/1/2/other=%lu/%lu/%lu/%lu\n",
      state->diagnostic.rejections[TOUCHPAD_REJECT_LIFT],
      state->diagnostic.rejections[TOUCHPAD_REJECT_PRESSURE],
      state->diagnostic.rejections[TOUCHPAD_REJECT_INVALID],
      state->diagnostic.rejections[TOUCHPAD_REJECT_COUNT],
      state->diagnostic.rejections[TOUCHPAD_REJECT_PALM],
      state->diagnostic.rejections[TOUCHPAD_REJECT_TYPING],
      state->diagnostic.rejections[TOUCHPAD_REJECT_EDGE],
      state->diagnostic.rejections[TOUCHPAD_REJECT_LOSS], state->diagnostic.contacts[0],
      state->diagnostic.contacts[1], state->diagnostic.contacts[2], state->diagnostic.contacts[3]);
  kinfo("synaptics-contact: Z-valid=%u Z=%u..%u bins <25/<30/<100/<220/rest=%lu/%lu/%lu/%lu/%lu W-valid=%u W=%u..%u bins <4/<8/<12/rest=%lu/%lu/%lu/%lu\n",
      state->diagnostic.have_pressure, state->diagnostic.pressure_min, state->diagnostic.pressure_max,
      state->diagnostic.pressure[0],
      state->diagnostic.pressure[1], state->diagnostic.pressure[2], state->diagnostic.pressure[3],
      state->diagnostic.pressure[4], state->diagnostic.have_width, state->diagnostic.width_min,
      state->diagnostic.width_max, state->diagnostic.width[0], state->diagnostic.width[1],
      state->diagnostic.width[2], state->diagnostic.width[3]);
  kinfo("synaptics-contact: coords-valid=%u X=%d..%d Y=%d..%d\n",
      state->diagnostic.have_coordinates, state->diagnostic.x_min, state->diagnostic.x_max,
      state->diagnostic.y_min, state->diagnostic.y_max);
  kinfo("synaptics-contact: pair usable/unusable/ambiguous/separated=%lu/%lu/%lu/%lu drag=%lu basic-W-scroll=%lu\n",
      state->diagnostic.paired, state->diagnostic.unusable_pairs, state->diagnostic.ambiguous_pairs,
      state->diagnostic.separated_pairs, state->diagnostic.drag_samples, state->diagnostic.basic_scroll_samples);
  state->diagnostic.paired = state->diagnostic.unusable_pairs = state->diagnostic.ambiguous_pairs = 0;
  state->diagnostic.separated_pairs = state->diagnostic.drag_samples = 0;
  state->diagnostic.basic_scroll_samples = 0;
  state->diagnostic.samples = state->diagnostic.accepted = state->diagnostic.resets = 0;
  for (unsigned i = 0; i < TOUCHPAD_REJECT_COUNT_TOTAL; i++) {
    state->diagnostic.rejections[i] = 0;
  }
  for (unsigned i = 0; i < 4; i++) {
    state->diagnostic.contacts[i] = state->diagnostic.width[i] = 0;
  }
  for (unsigned i = 0; i < 5; i++) {
    state->diagnostic.pressure[i] = 0;
  }
  state->diagnostic.spacing_min = state->diagnostic.spacing_max = 0;
  state->diagnostic.have_spacing = false;
  state->diagnostic.have_pressure = false;
  state->diagnostic.have_coordinates = false;
  state->diagnostic.have_width = false;
}

void touchpad_service(struct touchpad_state *state, uint64_t now)
{
  if (!state->diagnostic.enabled || state->diagnostic.finished) {
    return;
  }
  if (!state->diagnostic.started) {
    state->diagnostic.started = true;
    state->diagnostic.started_at = state->diagnostic.reported_at = now;
    kinfo("synaptics-contact: window=settle duration=10 s; authored tuning Z-on=%u Z-off=%u palm-Z=%u palm-W=%u edge=%u mm inward=%u mm gain=%u px/mm scroll-dead-zone=0.5 mm typing=%u ms pair-fresh=%u ms scroll-enter/leave=%u/%u mm\n",
        TOUCHPAD_PRESSURE_ON, TOUCHPAD_PRESSURE_OFF, TOUCHPAD_PALM_PRESSURE,
        TOUCHPAD_PALM_WIDTH, TOUCHPAD_EDGE_MM, TOUCHPAD_INWARD_MM,
        TOUCHPAD_PIXELS_PER_MM, CONFIG_TOUCHPAD_TYPING_MS,
        CONFIG_TOUCHPAD_PAIR_FRESH_MS, CONFIG_TOUCHPAD_SCROLL_ENTER_MM,
        CONFIG_TOUCHPAD_SCROLL_LEAVE_MM);
  }
  if (now < state->diagnostic.started_at) {
    return;
  }
  uint64_t elapsed = now - state->diagnostic.started_at;
  unsigned window = 0;
  if (elapsed >= TOUCHPAD_DIAGNOSTIC_SETTLE_NS) {
    uint64_t step = (elapsed - TOUCHPAD_DIAGNOSTIC_SETTLE_NS) /
        TOUCHPAD_DIAGNOSTIC_WINDOW_NS;
    window = step >= 7 ? 8 : (unsigned)step + 1;
  }
  if (window != state->diagnostic.window) {
    diagnostic_summary(state, elapsed);
    state->diagnostic.window = window;
    state->diagnostic.reported_at = now;
    kinfo("synaptics-contact: window=%s duration=%u s at=%lu s\n",
        diagnostic_windows[window], window == 8 ? 0 : 20,
        elapsed / TOUCHPAD_SECOND_NS);
    if (window == 8) {
      state->diagnostic.finished = true;
    }
  } else if (now - state->diagnostic.reported_at >= TOUCHPAD_DIAGNOSTIC_INTERVAL_NS) {
    diagnostic_summary(state, elapsed);
    state->diagnostic.reported_at = now;
  }
}
