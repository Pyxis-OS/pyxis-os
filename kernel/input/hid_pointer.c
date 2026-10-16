/* SPDX-License-Identifier: MPL-2.0 */
#include <abi/pointer.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/input/hid_pointer.h>

#define HID_MICROMETERS_PER_MM 1000
#define HID_AXIS_UM_LIMIT 1000000
#define HID_UNIT_CENTIMETERS 0x0011
#define HID_UNIT_INCHES 0x0013

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
    const struct hid_touchpad_profile *profile)
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
  struct mouse_profile geometry = {.absolute = true, .has_second = true,
      .native_contacts = true, .x_max = width, .y_max = height,
      .x_resolution = HID_MICROMETERS_PER_MM, .y_resolution = HID_MICROMETERS_PER_MM};
  touchpad_init(&pointer->filter, &geometry);
  touchpad_reset(&pointer->filter);
  pointer->source.touchpad = &pointer->filter;
  input_source_pointer_configure(&pointer->source, INPUT_POINTER_TOUCHPAD,
      POINTER_SCROLL_UNITS_PER_DETENT, HID_MICROMETERS_PER_MM * TOUCHPAD_SCROLL_MM_PER_DETENT,
      POINTER_SCROLL_UNITS_PER_DETENT, HID_MICROMETERS_PER_MM * TOUCHPAD_SCROLL_MM_PER_DETENT);
  cpu_restore_interrupts(flags);
  return true;
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
  /* A valid report also restores admission after isolated malformed input. */
  attach(pointer);
  if (pointer->initial_buttons_pending) {
    pointer->initial_buttons_pending = false;
    input_source_initial_pointer(&pointer->source, frame->buttons);
  }
  struct touchpad_motion motion;
  touchpad_report(&pointer->filter, &sample, frame->buttons,
      input_last_typing_ns(), now, &motion);
  if (motion.reset) {
    input_source_scroll_reset(&pointer->source);
  }
  input_pointer_report(&pointer->source, motion.dx, motion.dy,
      motion.scroll_x, motion.scroll_y, frame->buttons);
  cpu_restore_interrupts(flags);
}

void hid_pointer_lost(struct hid_pointer *pointer)
{
  uint64_t flags = cpu_save_interrupts();
  input_source_lost(&pointer->source);
  cpu_restore_interrupts(flags);
}
