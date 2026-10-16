/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/input/hid_touchpad.h>
#include <kernel/memory.h>

static bool field_value(const struct hid_layout_field *field,
    const uint8_t *data, size_t bytes, int64_t *value)
{
  if (!field->present || !field->width || field->width > 32 ||
      ((uint64_t)field->bit + field->width - 1) / 8 >= bytes) {
    return false;
  }
  uint32_t raw = 0;
  for (unsigned i = 0; i < field->width; ++i) {
    uint64_t bit = (uint64_t)field->bit + i;
    raw |= (uint32_t)((data[bit / 8] >> (bit % 8)) & 1) << i;
  }
  int64_t decoded = raw;
  if (field->minimum < 0 && (raw & (UINT32_C(1) << (field->width - 1)))) {
    decoded -= 1LL << field->width;
  }
  if (decoded < field->minimum || decoded > field->maximum) {
    return false;
  }
  *value = decoded;
  return true;
}

bool hid_touchpad_decode(const struct hid_touchpad_profile *profile,
    const uint8_t *report, size_t bytes, struct hid_touchpad_frame *frame,
    const char **reason)
{
  const char *failure = NULL;
  if (reason) {
    *reason = NULL;
  }
  if (!frame) {
    failure = "missing touchpad frame storage";
    goto failed;
  }
  memset(frame, 0, sizeof(*frame));
  if (!profile || !report || !bytes || bytes != profile->report_bytes ||
      !profile->slot_count || profile->slot_count > HID_TOUCHPAD_CONTACTS) {
    failure = "incomplete touchpad report or unadmitted profile";
    goto failed;
  }
  const uint8_t *data = report;
  size_t data_bytes = bytes;
  if (profile->numbered) {
    if (report[0] != profile->report_id) {
      failure = "wrong touchpad report ID";
      goto failed;
    }
    ++data;
    --data_bytes;
  }
  int64_t count, scan;
  if (!field_value(&profile->count, data, data_bytes, &count) ||
      count < 0 || count > profile->slot_count ||
      !field_value(&profile->scan_time, data, data_bytes, &scan) ||
      scan < 0 || scan > UINT16_MAX) {
    failure = "invalid touchpad contact count or scan time";
    goto failed;
  }
  frame->count = count;
  frame->scan_time = scan;
  for (unsigned i = 0; i < 3; ++i) {
    if (!profile->buttons[i].present) {
      continue;
    }
    int64_t pressed;
    if (!field_value(&profile->buttons[i], data, data_bytes, &pressed) ||
        (pressed != 0 && pressed != 1)) {
      failure = "invalid touchpad button state";
      goto failed;
    }
    if (pressed) {
      frame->buttons |= i == 2 ? HID_TOUCHPAD_SECONDARY : HID_TOUCHPAD_PRIMARY;
    }
  }
  for (unsigned i = 0; i < frame->count; ++i) {
    const struct hid_touchpad_contact_layout *contact = &profile->contacts[i];
    int64_t tip, confidence, id, x, y;
    if (!field_value(&contact->tip, data, data_bytes, &tip) || (tip != 0 && tip != 1) ||
        !field_value(&contact->confidence, data, data_bytes, &confidence) ||
        (confidence != 0 && confidence != 1) ||
        !field_value(&contact->id, data, data_bytes, &id) || id < 0 || id > UINT32_MAX ||
        !field_value(&contact->x, data, data_bytes, &x) || x < INT32_MIN || x > INT32_MAX ||
        !field_value(&contact->y, data, data_bytes, &y) || y < INT32_MIN || y > INT32_MAX) {
      failure = "invalid touchpad contact record";
      goto failed;
    }
    for (unsigned previous = 0; previous < i; ++previous) {
      if (frame->contacts[previous].id == (uint32_t)id) {
        failure = "duplicate touchpad contact ID";
        goto failed;
      }
    }
    frame->contacts[i] = (struct hid_touchpad_contact){
      .id = id, .x = x, .y = y, .tip = tip, .confidence = confidence,
    };
  }
  return true;

failed:
  if (frame) {
    memset(frame, 0, sizeof(*frame));
  }
  if (reason) {
    *reason = failure;
  }
  return false;
}
