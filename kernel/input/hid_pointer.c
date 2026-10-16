/* SPDX-License-Identifier: MPL-2.0 */
#include <abi/pointer.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/display.h>
#include <kernel/input/hid_pointer.h>

#define HID_MICROMETERS_PER_MM 1000
#define HID_AXIS_UM_LIMIT 1000000
#define HID_UNIT_CENTIMETERS 0x0011
#define HID_UNIT_INCHES 0x0013
#define HID_MOTION_PPM 1000000
#define HID_MOTION_MAX_GAP_NS UINT64_C(50000000)
#define HID_MOTION_SLOW_UM_PER_SECOND 10000
#define HID_MOTION_NORMAL_UM_PER_SECOND 100000
#define HID_MOTION_ACCEL_UM_PER_SECOND 250000
#define HID_MOTION_FAST_UM_PER_SECOND 400000
#define HID_MOTION_SLOW_GAIN 2000
#define HID_MOTION_NORMAL_GAIN 8333
#define HID_MOTION_FAST_GAIN 12500

static bool physical_span(const struct hid_layout_field *axis, int32_t *span)
{
  if (axis->physical_maximum <= axis->physical_minimum ||
      axis->physical_minimum < INT32_MIN || axis->physical_maximum > INT32_MAX ||
      axis->minimum < INT32_MIN || axis->maximum > INT32_MAX || axis->maximum <= axis->minimum) {
    return false;
  }
  uint64_t scale = axis->unit == HID_UNIT_CENTIMETERS ? 10000 :
      axis->unit == HID_UNIT_INCHES ? 25400 : 0;
  if (!scale || axis->exponent < -8 || axis->exponent > 7) {
    return false;
  }
  uint64_t numerator = (uint64_t)(axis->physical_maximum - axis->physical_minimum) * scale;
  uint64_t denominator = 1;
  for (int i = 0; i < axis->exponent; ++i) {
    if (numerator > UINT64_MAX / 10) {
      return false;
    }
    numerator *= 10;
  }
  for (int i = 0; i > axis->exponent; --i) {
    denominator *= 10;
  }
  uint64_t result = (numerator + denominator / 2) / denominator;
  /* Bound the filter's squared-distance arithmetic as well as its coordinates. */
  if (!result || result > HID_AXIS_UM_LIMIT) {
    return false;
  }
  *span = result;
  return true;
}

static int32_t coordinate(const struct hid_layout_field *axis, int32_t value, int32_t span)
{
  int64_t range = axis->maximum - axis->minimum;
  return ((int64_t)value - axis->minimum) * span / range;
}

bool hid_pointer_prepare(struct hid_pointer *pointer,
    const struct hid_touchpad_profile *profile, bool clickpad)
{
  int32_t width, height;
  if (!physical_span(&profile->contacts[0].x, &width) ||
      !physical_span(&profile->contacts[0].y, &height)) {
    return false;
  }
  uint64_t flags = cpu_save_interrupts();
  if (pointer->source.pointer_live) {
    cpu_restore_interrupts(flags);
    return false;
  }
  pointer->profile = *profile;
  pointer->width_um = width;
  pointer->height_um = height;
  hid_clickpad_init(&pointer->clickpad, clickpad, width, height);
  struct mouse_profile geometry = {.absolute = true, .has_second = true,
      .native_contacts = true, .x_max = width, .y_max = height,
      .x_resolution = HID_MICROMETERS_PER_MM, .y_resolution = HID_MICROMETERS_PER_MM};
  touchpad_init(&pointer->filter, &geometry);
  if (pointer->needs_lift) {
    touchpad_reset(&pointer->filter);
  }
  pointer->source.touchpad = &pointer->filter;
  input_source_pointer_configure(&pointer->source, INPUT_POINTER_TOUCHPAD,
      POINTER_SCROLL_UNITS_PER_DETENT, HID_MICROMETERS_PER_MM * TOUCHPAD_SCROLL_MM_PER_DETENT,
      POINTER_SCROLL_UNITS_PER_DETENT, HID_MICROMETERS_PER_MM * TOUCHPAD_SCROLL_MM_PER_DETENT);
  cpu_restore_interrupts(flags);
  return true;
}

static void reset_motion(struct hid_pointer *pointer)
{
  pointer->motion_at = 0;
  pointer->gain_fraction_x = pointer->gain_fraction_y = 0;
  pointer->pixel_fraction_x = pointer->pixel_fraction_y = 0;
}

static uint64_t square_root(uint64_t value)
{
  uint64_t result = 0, bit = UINT64_C(1) << 62;
  while (bit > value) {
    bit >>= 2;
  }
  while (bit) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

/* Millionths of an output width per physical millimetre, before user speed. */
static uint32_t motion_gain(uint64_t speed)
{
  if (speed <= HID_MOTION_SLOW_UM_PER_SECOND) {
    return HID_MOTION_SLOW_GAIN;
  }
  if (speed < HID_MOTION_NORMAL_UM_PER_SECOND) {
    return HID_MOTION_SLOW_GAIN + (speed - HID_MOTION_SLOW_UM_PER_SECOND) *
        (HID_MOTION_NORMAL_GAIN - HID_MOTION_SLOW_GAIN) /
        (HID_MOTION_NORMAL_UM_PER_SECOND - HID_MOTION_SLOW_UM_PER_SECOND);
  }
  if (speed <= HID_MOTION_ACCEL_UM_PER_SECOND) {
    return HID_MOTION_NORMAL_GAIN;
  }
  if (speed < HID_MOTION_FAST_UM_PER_SECOND) {
    return HID_MOTION_NORMAL_GAIN + (speed - HID_MOTION_ACCEL_UM_PER_SECOND) *
        (HID_MOTION_FAST_GAIN - HID_MOTION_NORMAL_GAIN) /
        (HID_MOTION_FAST_UM_PER_SECOND - HID_MOTION_ACCEL_UM_PER_SECOND);
  }
  return HID_MOTION_FAST_GAIN;
}

static int32_t project_axis(int32_t delta, uint64_t gain, uint32_t width,
    int64_t *gain_fraction, int64_t *pixel_fraction)
{
  /* Two bounded divisions preserve subpixel travel without multiplying the
   * full micrometre/gain/user-speed product by an arbitrary display width. */
  int64_t scaled = (int64_t)delta * gain + *gain_fraction;
  int64_t normalized = scaled / HID_MOTION_PPM;
  *gain_fraction = scaled % HID_MOTION_PPM;
  scaled = normalized * width + *pixel_fraction;
  int64_t pixels = scaled / HID_MOTION_PPM;
  *pixel_fraction = scaled % HID_MOTION_PPM;
  return pixels > INT32_MAX ? INT32_MAX : pixels < INT32_MIN ? INT32_MIN : (int32_t)pixels;
}

static void project_motion(struct hid_pointer *pointer, struct touchpad_motion *motion,
    uint64_t now)
{
  /* Geometry commits and this input adapter are BSP-owned; IF=0 prevents a
   * commit between the width/height snapshot and both-axis projection. */
  const struct framebuffer *output = display_layout();
  if (!output->width || output->width > INT32_MAX || !output->height ||
      output->height > INT32_MAX) {
    reset_motion(pointer);
    motion->dx = motion->dy = 0;
    return;
  }
  uint64_t previous = pointer->motion_at;
  if (motion->reset || pointer->output_width != output->width ||
      pointer->output_height != output->height) {
    reset_motion(pointer);
    previous = 0;
  }
  pointer->output_width = output->width;
  pointer->output_height = output->height;
  pointer->motion_at = now;
  uint64_t speed = 0;
  if (previous && now > previous && now - previous <= HID_MOTION_MAX_GAP_NS) {
    int64_t dx = motion->dx, dy = motion->dy;
    uint64_t distance = square_root(dx * dx + dy * dy);
    speed = distance * UINT64_C(1000000000) / (now - previous);
  }
  int32_t setting = input_touchpad_speed();
  int32_t multiplier = 1000 + (setting < 0 ? setting / 2 : setting);
  uint64_t gain = (uint64_t)motion_gain(speed) * multiplier;
  motion->dx = project_axis(motion->dx, gain, pointer->output_width,
      &pointer->gain_fraction_x, &pointer->pixel_fraction_x);
  motion->dy = project_axis(motion->dy, gain, pointer->output_width,
      &pointer->gain_fraction_y, &pointer->pixel_fraction_y);
}

static void attach(struct hid_pointer *pointer)
{
  if (!pointer->source.pointer_live) {
    pointer->initial_buttons_pending = true;
    input_source_attach(&pointer->source, false, true);
  }
}

void hid_pointer_ready(struct hid_pointer *pointer)
{
  uint64_t flags = cpu_save_interrupts();
  attach(pointer);
  cpu_restore_interrupts(flags);
}

void hid_pointer_report(struct hid_pointer *pointer, const struct hid_touchpad_frame *frame)
{
  struct touchpad_contact contacts[HID_TOUCHPAD_CONTACTS];
  for (unsigned i = 0; i < frame->count; ++i) {
    const struct hid_touchpad_contact *contact = &frame->contacts[i];
    contacts[i] = (struct touchpad_contact){
      .x = coordinate(&pointer->profile.contacts[0].x, contact->x, pointer->width_um),
      .y = pointer->height_um -
          coordinate(&pointer->profile.contacts[0].y, contact->y, pointer->height_um),
      .id = contact->id,
      .flags = TOUCHPAD_CONTACT_ID_VALID |
          (contact->tip ? TOUCHPAD_CONTACT_TIP : 0) |
          (contact->confidence ? TOUCHPAD_CONTACT_CONFIDENCE : 0),
    };
  }
  uint64_t now = arch_monotonic_ns();
  struct touchpad_sample sample = {.contacts = contacts, .contact_count = frame->count,
      .contacts_complete = true, .valid = true, .observed_at = now};
  uint64_t flags = cpu_save_interrupts();
  /* Complete reports are the only evidence of released contacts/buttons.
   * Unknown IDs never reach this adapter; uncertain loss overrides all-up. */
  pointer->needs_lift = frame->buttons != 0;
  for (unsigned i = 0; i < frame->count; ++i) {
    pointer->needs_lift |= frame->contacts[i].tip;
  }
  /* A valid report also restores admission after isolated malformed input. */
  attach(pointer);
  uint32_t buttons = hid_clickpad_report(&pointer->clickpad, contacts, frame->count,
      frame->buttons, !pointer->filter.loss_quarantine && !pointer->filter.palm);
  if (pointer->initial_buttons_pending) {
    pointer->initial_buttons_pending = false;
    input_source_initial_pointer(&pointer->source, buttons);
  }
  struct touchpad_motion motion;
  touchpad_report(&pointer->filter, &sample, frame->buttons,
      input_last_typing_ns(), now, &motion);
  if (motion.reset) {
    input_source_scroll_reset(&pointer->source);
  }
  project_motion(pointer, &motion, now);
  input_pointer_report(&pointer->source, motion.dx, motion.dy,
      motion.scroll_x, motion.scroll_y, buttons);
  cpu_restore_interrupts(flags);
}

void hid_pointer_lost(struct hid_pointer *pointer, bool uncertain)
{
  uint64_t flags = cpu_save_interrupts();
  pointer->needs_lift |= uncertain;
  input_source_lost(&pointer->source);
  reset_motion(pointer);
  hid_clickpad_init(&pointer->clickpad, pointer->clickpad.enabled,
      pointer->width_um, pointer->height_um);
  cpu_restore_interrupts(flags);
}
