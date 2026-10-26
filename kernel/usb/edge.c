#include "edge.h"

#define EDGE_INPUT_ID 1
#define EDGE_OUTPUT_ID 2
#define EDGE_REPORT_IDS 41
#define EDGE_FEATURE_REPORTS 39
#define EDGE_INPUT_FIELDS 6
#define EDGE_PAYLOAD_BITS ((USB_EDGE_REPORT_BYTES - 1) * 8)
#define HID_PAGE_DESKTOP 0x01
#define HID_PAGE_BUTTON 0x09
#define HID_PAGE_VENDOR 0xff00
#define HID_USAGE_GAMEPAD 0x05
#define HID_MAIN_VARIABLE 0x02
#define HID_MAIN_NULL_VARIABLE 0x42
#define HID_UNIT_DEGREES 0x14
#define EDGE_AXES_OFFSET 1
#define EDGE_HAT_OFFSET 8
#define EDGE_BUTTONS_OFFSET 9
#define EDGE_EXTRA_BUTTONS_OFFSET 10
#define EDGE_HAT_MASK 0x0f
#define EDGE_BASE_BUTTONS_MASK 0x07
#define EDGE_EXTRA_BUTTONS_MASK 0xf0

struct edge_descriptor_state {
  uint32_t page, minimum, maximum, physical_minimum, physical_maximum;
  uint32_t unit, unit_exponent, size, count, id;
  uint32_t usages[GAMEPAD_AXIS_COUNT], usage_minimum, usage_maximum;
  unsigned usage_count;
  bool range_minimum, range_maximum;
};

static bool input_field(const struct edge_descriptor_state *state, unsigned field, uint32_t flags)
{
  static const struct {
    uint16_t page, maximum;
    uint8_t size, count, usage;
  } fields[EDGE_INPUT_FIELDS] = {
    {HID_PAGE_DESKTOP, 255, 8, 6, 0x30},
    {HID_PAGE_VENDOR, 255, 8, 1, 0x20},
    {HID_PAGE_DESKTOP, 7, 4, 1, 0x39},
    {HID_PAGE_BUTTON, 1, 1, 15, 0},
    {HID_PAGE_VENDOR, 1, 1, 13, 0x21},
    {HID_PAGE_VENDOR, 255, 8, 52, 0x22},
  };
  static const uint8_t axes[GAMEPAD_AXIS_COUNT] = {0x30, 0x31, 0x32, 0x35, 0x33, 0x34};
  if (field >= EDGE_INPUT_FIELDS || state->id != EDGE_INPUT_ID ||
      state->page != fields[field].page || state->minimum ||
      state->maximum != fields[field].maximum || state->size != fields[field].size ||
      state->count != fields[field].count ||
      flags != (field == 2 ? HID_MAIN_NULL_VARIABLE : HID_MAIN_VARIABLE)) {
    return false;
  }
  if (field == 2 && (state->physical_minimum || state->physical_maximum != 315 ||
      state->unit != HID_UNIT_DEGREES || state->unit_exponent)) {
    return false;
  }
  if (field != 2 && (state->unit || state->unit_exponent)) {
    return false;
  }
  if (field == 3) {
    return !state->usage_count && state->range_minimum && state->range_maximum &&
      state->usage_minimum == ((HID_PAGE_BUTTON << 16) | 1) &&
      state->usage_maximum == ((HID_PAGE_BUTTON << 16) | 15);
  }
  if (state->range_minimum || state->range_maximum ||
      state->usage_count != (field == 0 ? GAMEPAD_AXIS_COUNT : 1)) {
    return false;
  }
  for (unsigned index = 0; index < state->usage_count; ++index) {
    uint32_t usage = field == 0 ? axes[index] : fields[field].usage;
    if (state->usages[index] != ((state->page << 16) | usage)) {
      return false;
    }
  }
  return true;
}

bool usb_edge_descriptor(const uint8_t *descriptor, size_t bytes)
{
  if (bytes != USB_EDGE_DESCRIPTOR_BYTES) {
    return false;
  }
  struct edge_descriptor_state state = {0};
  uint8_t ids[32] = {0};
  unsigned reports = 0, features = 0, outputs = 0, inputs = 0, input_bits = 0;
  bool collection = false, closed = false, report_has_main = false;
  /* This profile accepts the captured flat Application collection grammar.
   * Unused vendor reports are bounded, not expanded into mouse layouts. */
  for (size_t offset = 0; offset < bytes;) {
    uint8_t prefix = descriptor[offset++];
    unsigned size = prefix & 3;
    if (size == 3) {
      size = 4;
    }
    if (prefix == 0xfe || size > bytes - offset || closed) {
      return false;
    }
    uint32_t value = 0;
    for (unsigned index = 0; index < size; ++index) {
      value |= (uint32_t)descriptor[offset++] << (index * 8);
    }
    switch (prefix & 0xfc) {
    case 0x04: state.page = value; break;
    case 0x14: state.minimum = value; break;
    case 0x24: state.maximum = value; break;
    case 0x34: state.physical_minimum = value; break;
    case 0x44: state.physical_maximum = value; break;
    case 0x54: state.unit_exponent = value; break;
    case 0x64: state.unit = value; break;
    case 0x74: state.size = value; break;
    case 0x94: state.count = value; break;
    case 0x84:
      if (!collection || !value || value > UINT8_MAX ||
          (reports && !report_has_main) || (ids[value / 8] & (1u << (value % 8)))) {
        return false;
      }
      ids[value / 8] |= 1u << (value % 8);
      state.id = value;
      report_has_main = false;
      ++reports;
      break;
    case 0x08:
      if (state.usage_count == GAMEPAD_AXIS_COUNT || size > 2 || state.page > UINT16_MAX) {
        return false;
      }
      state.usages[state.usage_count++] = (state.page << 16) | value;
      break;
    case 0x18:
      if (state.range_minimum || size > 2 || state.page > UINT16_MAX) {
        return false;
      }
      state.range_minimum = true;
      state.usage_minimum = (state.page << 16) | value;
      break;
    case 0x28:
      if (state.range_maximum || size > 2 || state.page > UINT16_MAX) {
        return false;
      }
      state.range_maximum = true;
      state.usage_maximum = (state.page << 16) | value;
      break;
    case 0xa0:
      if (collection || reports || value != 1 || state.page != HID_PAGE_DESKTOP ||
          state.usage_count != 1 ||
          state.usages[0] != ((HID_PAGE_DESKTOP << 16) | HID_USAGE_GAMEPAD) ||
          state.range_minimum || state.range_maximum) {
        return false;
      }
      collection = true;
      break;
    case 0xc0:
      if (!collection || size || !report_has_main) {
        return false;
      }
      closed = true;
      break;
    case 0x80:
      if (!collection || !input_field(&state, inputs, value)) {
        return false;
      }
      input_bits += state.size * state.count;
      ++inputs;
      report_has_main = true;
      break;
    case 0x90: case 0xb0:
      if (!collection || !state.id || report_has_main ||
          inputs != EDGE_INPUT_FIELDS || state.page != HID_PAGE_VENDOR ||
          state.minimum || state.maximum != 255 || state.size != 8 ||
          !state.count || state.count > USB_EDGE_REPORT_BYTES - 1 ||
          value != HID_MAIN_VARIABLE || state.unit || state.unit_exponent ||
          state.usage_count != 1 || (state.usages[0] >> 16) != HID_PAGE_VENDOR ||
          state.range_minimum || state.range_maximum) {
        return false;
      }
      if ((prefix & 0xfc) == 0x90) {
        if (state.id != EDGE_OUTPUT_ID || state.count != USB_EDGE_REPORT_BYTES - 1) {
          return false;
        }
        ++outputs;
      } else {
        if (state.id == EDGE_INPUT_ID || state.id == EDGE_OUTPUT_ID) {
          return false;
        }
        ++features;
      }
      report_has_main = true;
      break;
    default:
      return false;
    }
    if (!(prefix & 0x0c)) {
      state.usage_count = 0;
      state.range_minimum = state.range_maximum = false;
    }
  }
  return closed && reports == EDGE_REPORT_IDS && features == EDGE_FEATURE_REPORTS &&
    outputs == 1 && inputs == EDGE_INPUT_FIELDS && input_bits == EDGE_PAYLOAD_BITS;
}

bool usb_edge_decode(const uint8_t *report, size_t bytes, struct gamepad_state *state)
{
  if (bytes != USB_EDGE_REPORT_BYTES || report[0] != EDGE_INPUT_ID) {
    return false;
  }
  struct gamepad_state decoded = {0};
  for (unsigned index = 0; index < GAMEPAD_AXIS_COUNT; ++index) {
    decoded.axes[index] = report[EDGE_AXES_OFFSET + index];
  }
  decoded.hat = report[EDGE_HAT_OFFSET] & EDGE_HAT_MASK;
  if (decoded.hat > GAMEPAD_HAT_NEUTRAL) {
    decoded.hat = GAMEPAD_HAT_NEUTRAL;
  }
  /* Buttons 1..15 precede one reserved bit and the four Edge vendor buttons.
   * Linux hid-playstation names bit 1 touchpad and bit 2 microphone. */
  decoded.buttons = (report[EDGE_HAT_OFFSET] >> 4) |
    ((uint32_t)report[EDGE_BUTTONS_OFFSET] << 4) |
    ((uint32_t)(report[EDGE_EXTRA_BUTTONS_OFFSET] & EDGE_BASE_BUTTONS_MASK) << 12) |
    ((uint32_t)(report[EDGE_EXTRA_BUTTONS_OFFSET] & EDGE_EXTRA_BUTTONS_MASK) << 11);
  *state = decoded;
  return true;
}
