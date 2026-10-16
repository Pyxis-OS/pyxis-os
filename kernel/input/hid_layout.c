/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/input/hid_layout.h>
#include <kernel/input/hid_touchpad.h>
#include <kernel/memory.h>

#define HID_LAYOUT_DEPTH 16
#define HID_LAYOUT_USAGE_SPANS 32
#define HID_ITEM_LONG 0xfe
#define HID_ITEM_MAIN 0
#define HID_ITEM_GLOBAL 1
#define HID_ITEM_LOCAL 2
#define HID_MAIN_INPUT 8
#define HID_MAIN_OUTPUT 9
#define HID_MAIN_COLLECTION 10
#define HID_MAIN_FEATURE 11
#define HID_MAIN_END_COLLECTION 12
#define HID_DATA_CONSTANT (1u << 0)
#define HID_DATA_VARIABLE (1u << 1)
#define HID_DATA_RELATIVE (1u << 2)
#define HID_DATA_WRAP (1u << 3)
#define HID_DATA_NONLINEAR (1u << 4)
#define HID_DATA_NULL (1u << 6)
#define HID_DATA_BUFFERED (1u << 8)
#define HID_DATA_FLAGS_MASK UINT32_C(0x1ff)
#define HID_COLLECTION_APPLICATION 1
#define HID_COLLECTION_LOGICAL 2
#define HID_USAGE_TOUCHPAD 0x000d0005
#define HID_USAGE_FINGER 0x000d0022
#define HID_USAGE_TIP 0x000d0042
#define HID_USAGE_CONFIDENCE 0x000d0047
#define HID_USAGE_CONTACT_ID 0x000d0051
#define HID_USAGE_CONFIGURATION 0x000d000e
#define HID_USAGE_INPUT_MODE 0x000d0052
#define HID_USAGE_CONTACT_COUNT 0x000d0054
#define HID_USAGE_CONTACT_MAXIMUM 0x000d0055
#define HID_USAGE_SCAN_TIME 0x000d0056
#define HID_USAGE_BUTTON_TYPE 0x000d0059
#define HID_USAGE_X 0x00010030
#define HID_USAGE_Y 0x00010031
#define HID_USAGE_BUTTON_FIRST 0x00090001
#define HID_NO_CONTACT UINT8_MAX
#define HID_UNIT_CENTIMETERS 0x0011
#define HID_UNIT_INCHES 0x0013
#define HID_UNIT_SECONDS 0x1001

struct hid_layout_global {
  uint32_t page, size, count, unit;
  int64_t minimum, maximum, physical_minimum, physical_maximum;
  int8_t exponent;
  uint8_t id;
  bool have_minimum, have_maximum, invalid_exponent;
};

struct hid_layout_usage {
  /* Preserve usage ordering without expanding descriptor-supplied ranges. */
  uint32_t first, last;
};

struct hid_layout_collection {
  uint8_t contact;
  bool configuration, touchpad;
};

struct hid_layout_parser {
  struct hid_layout_global global, globals[HID_LAYOUT_DEPTH];
  struct hid_layout_usage usages[HID_LAYOUT_USAGE_SPANS];
  struct hid_layout_collection collections[HID_LAYOUT_DEPTH];
  unsigned global_depth, collection_depth, usage_count;
  bool range_open, touchpad_seen, touchpad_input;
};

static uint32_t item_value(const uint8_t *data, unsigned bytes)
{
  uint32_t value = 0;
  for (unsigned i = 0; i < bytes; ++i) {
    value |= (uint32_t)data[i] << (i * 8);
  }
  return value;
}

static int64_t signed_value(uint32_t value, unsigned bytes)
{
  unsigned bits = bytes * 8;
  return (value & (1u << (bits - 1))) ? (int64_t)value - (1LL << bits) : value;
}

static const char *global_item(struct hid_layout_parser *parser, unsigned tag,
    uint32_t value, unsigned bytes)
{
  struct hid_layout_global *global = &parser->global;
  if (tag == 10) {
    if (bytes || parser->global_depth == HID_LAYOUT_DEPTH) {
      return "global PUSH shape or budget";
    }
    parser->globals[parser->global_depth++] = *global;
    return NULL;
  }
  if (tag == 11) {
    if (bytes || !parser->global_depth) {
      return "global POP shape or underflow";
    }
    *global = parser->globals[--parser->global_depth];
    return NULL;
  }
  if (!bytes) {
    return "empty global value";
  }
  switch (tag) {
  case 0:
    if (value > UINT16_MAX) {
      return "usage page exceeds 16 bits";
    }
    global->page = value;
    return NULL;
  case 1:
    global->minimum = signed_value(value, bytes);
    global->have_minimum = true;
    return NULL;
  case 2:
    global->maximum = global->minimum < 0 ? signed_value(value, bytes) : value;
    global->have_maximum = true;
    return NULL;
  case 3: global->physical_minimum = signed_value(value, bytes); return NULL;
  case 4:
    global->physical_maximum = global->physical_minimum < 0 ? signed_value(value, bytes) : value;
    return NULL;
  case 5:
    /* The unit exponent is a signed nibble, including when the item is wider. */
    global->invalid_exponent = value > 15 &&
        (signed_value(value, bytes) < -8 || signed_value(value, bytes) >= 0);
    global->exponent = value & 8 ? (int)(value & 15) - 16 : (int)(value & 7);
    return NULL;
  case 6: global->unit = value; return NULL;
  case 7: global->size = value; return NULL;
  case 8:
    if (!value || value > UINT8_MAX) {
      return "invalid report ID";
    }
    global->id = value;
    return NULL;
  case 9: global->count = value; return NULL;
  default: return "reserved global item";
  }
}

static const char *local_item(struct hid_layout_parser *parser, unsigned tag,
    uint32_t value, unsigned bytes)
{
  if (!bytes) {
    return "empty local value";
  }
  if (tag == 10) {
    return "usage delimiters unsupported";
  }
  if (tag == 3 || tag == 4 || tag == 5 || tag == 7 || tag == 8 || tag == 9) {
    return NULL; /* Designators and strings do not alter the bit layout. */
  }
  if (tag > 2) {
    return "reserved local item";
  }
  uint32_t usage = bytes == 4 ? value : (parser->global.page << 16) | value;
  if (tag == 2) {
    if (!parser->range_open) {
      return "usage maximum without minimum";
    }
    struct hid_layout_usage *range = &parser->usages[parser->usage_count - 1];
    if (usage < range->first || (usage >> 16) != (range->first >> 16)) {
      return "invalid usage range";
    }
    range->last = usage;
    parser->range_open = false;
    return NULL;
  }
  if (parser->range_open || parser->usage_count == HID_LAYOUT_USAGE_SPANS) {
    return "usage order or span budget";
  }
  parser->usages[parser->usage_count++] = (struct hid_layout_usage){usage, usage};
  parser->range_open = tag == 1;
  return NULL;
}

static const char *input_mode(struct hid_layout_parser *parser,
    struct hid_report_layout *layout, uint32_t offset, uint32_t flags)
{
  const struct hid_layout_global *global = &parser->global;
  uint64_t index = 0;
  for (unsigned i = 0; i < parser->usage_count; ++i) {
    const struct hid_layout_usage *usage = &parser->usages[i];
    uint64_t count = (uint64_t)usage->last - usage->first + 1;
    if (HID_USAGE_INPUT_MODE >= usage->first && HID_USAGE_INPUT_MODE <= usage->last) {
      if (!(flags & HID_DATA_VARIABLE)) {
        return "Input Mode arrays unsupported";
      }
      uint64_t field = index + HID_USAGE_INPUT_MODE - usage->first;
      if (field < global->count) {
        if (layout->mode_present ||
            (i + 1 == parser->usage_count && usage->last == HID_USAGE_INPUT_MODE &&
             (uint64_t)global->count > index + count)) {
          return "ambiguous Input Mode feature";
        }
        if ((flags & (HID_DATA_CONSTANT | HID_DATA_RELATIVE | HID_DATA_BUFFERED)) ||
            global->size < 2 || global->size > 32 ||
            !global->have_minimum || !global->have_maximum ||
            global->minimum < 0 || global->minimum > 3 || global->maximum < 3 ||
            (uint64_t)global->maximum > ((1ULL << global->size) - 1)) {
          return "unsupported Input Mode feature shape or range";
        }
        layout->mode_present = true;
        layout->mode_id = global->id;
        layout->mode_width = global->size;
        layout->mode_bit = offset + field * global->size;
      }
    }
    index += count;
  }
  return NULL;
}

static const char *retain_field(const struct hid_layout_parser *parser,
    struct hid_layout_field *field, uint32_t usage, uint32_t offset, uint32_t flags)
{
  const struct hid_layout_global *global = &parser->global;
  uint64_t index = 0;
  for (unsigned i = 0; i < parser->usage_count; ++i) {
    const struct hid_layout_usage *span = &parser->usages[i];
    uint64_t count = (uint64_t)span->last - span->first + 1;
    if (usage >= span->first && usage <= span->last) {
      if (!(flags & HID_DATA_VARIABLE)) {
        return "touchpad array field unsupported";
      }
      uint64_t position = index + usage - span->first;
      if (position < global->count) {
        if (field->present ||
            (i + 1 == parser->usage_count && span->last == usage &&
             (uint64_t)global->count > index + count)) {
          return "ambiguous touchpad scalar field";
        }
        if ((flags & (HID_DATA_CONSTANT | HID_DATA_RELATIVE | HID_DATA_WRAP |
                      HID_DATA_NONLINEAR | HID_DATA_NULL | HID_DATA_BUFFERED)) ||
            (flags & ~HID_DATA_FLAGS_MASK) || !global->size || global->size > 32 ||
            !global->have_minimum || !global->have_maximum ||
            global->minimum > global->maximum || global->invalid_exponent) {
          return "unsupported touchpad scalar field";
        }
        int64_t minimum = global->minimum < 0 ? -(1LL << (global->size - 1)) : 0;
        int64_t maximum = global->minimum < 0 ? (1LL << (global->size - 1)) - 1 :
            (1LL << global->size) - 1;
        if (global->minimum < minimum || global->maximum > maximum) {
          return "touchpad logical range exceeds field width";
        }
        *field = (struct hid_layout_field){
          .minimum = global->minimum, .maximum = global->maximum,
          .physical_minimum = global->physical_minimum,
          .physical_maximum = global->physical_maximum,
          .unit = global->unit, .exponent = global->exponent,
          .id = global->id, .bit = offset + position * global->size,
          .width = global->size, .present = true,
        };
      }
    }
    index += count;
  }
  return NULL;
}

static const char *touchpad_item(struct hid_layout_parser *parser,
    struct hid_touchpad_profile *profile, enum hid_layout_type type,
    uint32_t offset, uint32_t flags)
{
  const struct hid_layout_collection *collection =
      &parser->collections[parser->collection_depth - 1];
  if (!profile || !collection->touchpad || type == HID_LAYOUT_OUTPUT) {
    return NULL;
  }
  if (type == HID_LAYOUT_FEATURE) {
    const char *failure = retain_field(parser, &profile->count_maximum,
        HID_USAGE_CONTACT_MAXIMUM, offset, flags);
    return failure ? failure : retain_field(parser, &profile->button_type,
        HID_USAGE_BUTTON_TYPE, offset, flags);
  }
  if (parser->touchpad_input && profile->report_id != parser->global.id) {
    return "multiple touchpad input reports unsupported";
  }
  parser->touchpad_input = true;
  profile->report_id = parser->global.id;
  const char *failure;
  if (collection->contact != HID_NO_CONTACT) {
    struct hid_touchpad_contact_layout *contact = &profile->contacts[collection->contact];
    failure = retain_field(parser, &contact->tip, HID_USAGE_TIP, offset, flags);
    if (!failure) {
      failure = retain_field(parser, &contact->confidence, HID_USAGE_CONFIDENCE, offset, flags);
    }
    if (!failure) {
      failure = retain_field(parser, &contact->id, HID_USAGE_CONTACT_ID, offset, flags);
    }
    if (!failure) {
      failure = retain_field(parser, &contact->x, HID_USAGE_X, offset, flags);
    }
    return failure ? failure : retain_field(parser, &contact->y, HID_USAGE_Y, offset, flags);
  }
  failure = retain_field(parser, &profile->count, HID_USAGE_CONTACT_COUNT, offset, flags);
  if (!failure) {
    failure = retain_field(parser, &profile->scan_time, HID_USAGE_SCAN_TIME, offset, flags);
  }
  for (unsigned i = 0; !failure && i < 3; ++i) {
    failure = retain_field(parser, &profile->buttons[i], HID_USAGE_BUTTON_FIRST + i,
        offset, flags);
  }
  return failure;
}

static bool field_bounds(const struct hid_report_layout *layout,
    const struct hid_layout_field *field, enum hid_layout_type type)
{
  return field->present && field->width && field->width <= 32 &&
      (uint64_t)field->bit + field->width <= layout->bits[type][field->id];
}

static bool boolean_field(const struct hid_report_layout *layout,
    const struct hid_layout_field *field)
{
  return field_bounds(layout, field, HID_LAYOUT_INPUT) && field->width == 1 &&
      field->minimum == 0 && field->maximum == 1;
}

static bool axis_field(const struct hid_report_layout *layout,
    const struct hid_layout_field *field)
{
  return field_bounds(layout, field, HID_LAYOUT_INPUT) &&
      field->minimum >= INT32_MIN && field->maximum <= INT32_MAX &&
      field->maximum > field->minimum && field->physical_maximum > field->physical_minimum &&
      field->physical_minimum >= INT32_MIN && field->physical_maximum <= INT32_MAX &&
      (field->unit == HID_UNIT_CENTIMETERS || field->unit == HID_UNIT_INCHES);
}

static bool same_axis(const struct hid_layout_field *first, const struct hid_layout_field *other)
{
  return first->minimum == other->minimum && first->maximum == other->maximum &&
      first->physical_minimum == other->physical_minimum &&
      first->physical_maximum == other->physical_maximum &&
      first->unit == other->unit && first->exponent == other->exponent;
}

static const char *touchpad_finish(const struct hid_layout_parser *parser,
    const struct hid_report_layout *layout, struct hid_touchpad_profile *profile)
{
  if (!parser->touchpad_seen || !parser->touchpad_input || !profile->slot_count) {
    return "missing touchpad application or contact slots";
  }
  if (!field_bounds(layout, &profile->count, HID_LAYOUT_INPUT) || profile->count.minimum != 0 ||
      profile->count.maximum < 1 ||
      !field_bounds(layout, &profile->count_maximum, HID_LAYOUT_FEATURE) ||
      profile->count_maximum.minimum < 0 || profile->count_maximum.maximum < 1) {
    return "missing or invalid contact count fields";
  }
  const struct hid_layout_field *scan = &profile->scan_time;
  if (!field_bounds(layout, scan, HID_LAYOUT_INPUT) || scan->width != 16 ||
      scan->minimum != 0 || scan->maximum != UINT16_MAX ||
      scan->unit != HID_UNIT_SECONDS || scan->exponent != -4) {
    return "touchpad scan time must be a 16-bit 100-us counter";
  }
  if (profile->button_type.present &&
      (!field_bounds(layout, &profile->button_type, HID_LAYOUT_FEATURE) ||
       profile->button_type.minimum < 0)) {
    return "invalid touchpad button type field";
  }
  for (unsigned i = 0; i < 3; ++i) {
    if (profile->buttons[i].present && !boolean_field(layout, &profile->buttons[i])) {
      return "invalid touchpad button field";
    }
  }
  for (unsigned i = 0; i < profile->slot_count; ++i) {
    const struct hid_touchpad_contact_layout *contact = &profile->contacts[i];
    if (!boolean_field(layout, &contact->tip) || !boolean_field(layout, &contact->confidence) ||
        !field_bounds(layout, &contact->id, HID_LAYOUT_INPUT) || contact->id.minimum < 0 ||
        !axis_field(layout, &contact->x) || !axis_field(layout, &contact->y)) {
      return "missing or invalid touchpad contact field";
    }
    if (!same_axis(&profile->contacts[0].x, &contact->x) ||
        !same_axis(&profile->contacts[0].y, &contact->y)) {
      return "touchpad contact geometry differs between slots";
    }
  }
  profile->numbered = layout->numbered[HID_LAYOUT_INPUT];
  profile->report_bytes = ((uint64_t)layout->bits[HID_LAYOUT_INPUT][profile->report_id] + 7) / 8 +
      profile->numbered;
  return NULL;
}

static const char *data_item(struct hid_layout_parser *parser,
    struct hid_report_layout *layout, enum hid_layout_type type, uint32_t flags,
    unsigned bytes, struct hid_touchpad_profile *profile)
{
  const struct hid_layout_global *global = &parser->global;
  if (!bytes || !parser->collection_depth || !global->size || !global->count) {
    return "invalid report data item";
  }
  uint64_t bits = (uint64_t)global->size * global->count;
  uint32_t offset = layout->bits[type][global->id];
  if (bits > UINT32_MAX - offset) {
    return "report bit length overflow";
  }
  layout->bits[type][global->id] += bits;
  if (global->id) {
    layout->numbered[type] = true;
  }
  if (type == HID_LAYOUT_FEATURE &&
      parser->collections[parser->collection_depth - 1].configuration) {
    const char *failure = input_mode(parser, layout, offset, flags);
    if (failure) {
      return failure;
    }
  }
  return touchpad_item(parser, profile, type, offset, flags);
}

static const char *main_item(struct hid_layout_parser *parser,
    struct hid_report_layout *layout, unsigned tag, uint32_t value, unsigned bytes,
    struct hid_touchpad_profile *profile)
{
  if (parser->range_open) {
    return "unfinished usage range";
  }
  switch (tag) {
  case HID_MAIN_INPUT:
    return data_item(parser, layout, HID_LAYOUT_INPUT, value, bytes, profile);
  case HID_MAIN_OUTPUT:
    return data_item(parser, layout, HID_LAYOUT_OUTPUT, value, bytes, profile);
  case HID_MAIN_FEATURE:
    return data_item(parser, layout, HID_LAYOUT_FEATURE, value, bytes, profile);
  case HID_MAIN_COLLECTION: {
    if (bytes != 1 || parser->collection_depth == HID_LAYOUT_DEPTH) {
      return "collection shape or nesting budget";
    }
    struct hid_layout_collection collection = {.contact = HID_NO_CONTACT};
    uint32_t usage = parser->usage_count ? parser->usages[0].first : 0;
    if (parser->collection_depth) {
      collection = parser->collections[parser->collection_depth - 1];
    } else if (value == HID_COLLECTION_APPLICATION) {
      collection.configuration = usage == HID_USAGE_CONFIGURATION;
      collection.touchpad = profile && usage == HID_USAGE_TOUCHPAD;
      if (collection.touchpad) {
        if (parser->touchpad_seen) {
          return "multiple touchpad applications unsupported";
        }
        parser->touchpad_seen = true;
      }
    }
    if (collection.touchpad && usage == HID_USAGE_FINGER) {
      if (value != HID_COLLECTION_LOGICAL || collection.contact != HID_NO_CONTACT ||
          profile->slot_count == HID_TOUCHPAD_CONTACTS) {
        return "touchpad Finger collection shape or slot budget";
      }
      collection.contact = profile->slot_count++;
    }
    parser->collections[parser->collection_depth++] = collection;
    return NULL;
  }
  case HID_MAIN_END_COLLECTION:
    if (bytes || !parser->collection_depth) {
      return "END_COLLECTION shape or underflow";
    }
    --parser->collection_depth;
    return NULL;
  default: return "reserved main item";
  }
}

static bool parse(struct hid_report_layout *layout, const uint8_t *descriptor,
    size_t bytes, struct hid_touchpad_profile *profile, const char **reason)
{
  const char *failure = NULL;
  if (reason) {
    *reason = NULL;
  }
  if (profile) {
    memset(profile, 0, sizeof(*profile));
  }
  if (!layout) {
    failure = "missing layout storage";
    goto failed;
  }
  memset(layout, 0, sizeof(*layout));
  if (!descriptor || !bytes) {
    failure = "empty report descriptor";
    goto failed;
  }
  struct hid_layout_parser parser = {0};
  for (size_t offset = 0; offset < bytes;) {
    uint8_t prefix = descriptor[offset++];
    unsigned size = prefix & 3;
    size = size == 3 ? 4 : size;
    if (prefix == HID_ITEM_LONG || size > bytes - offset) {
      failure = prefix == HID_ITEM_LONG ? "long items unsupported" : "truncated item";
      goto failed;
    }
    uint32_t value = item_value(descriptor + offset, size);
    offset += size;
    unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
    if (type == HID_ITEM_GLOBAL) {
      failure = global_item(&parser, tag, value, size);
    } else if (type == HID_ITEM_LOCAL) {
      failure = local_item(&parser, tag, value, size);
    } else if (type == HID_ITEM_MAIN) {
      failure = main_item(&parser, layout, tag, value, size, profile);
      parser.usage_count = 0;
    } else {
      failure = "reserved item type";
    }
    if (failure) {
      goto failed;
    }
  }
  if (parser.collection_depth || parser.global_depth || parser.usage_count || parser.range_open) {
    failure = "unfinished descriptor state";
    goto failed;
  }
  bool reports = false;
  for (unsigned type = 0; type < HID_LAYOUT_TYPES; ++type) {
    if (layout->numbered[type] && layout->bits[type][0]) {
      failure = "mixed numbered and unnumbered reports";
      goto failed;
    }
    for (unsigned id = 0; id <= UINT8_MAX; ++id) {
      reports |= layout->bits[type][id] != 0;
    }
  }
  if (!reports) {
    failure = "descriptor has no reports";
    goto failed;
  }
  if (profile) {
    failure = touchpad_finish(&parser, layout, profile);
    if (failure) {
      goto failed;
    }
  }
  return true;

failed:
  if (layout) {
    memset(layout, 0, sizeof(*layout));
  }
  if (profile) {
    memset(profile, 0, sizeof(*profile));
  }
  if (reason) {
    *reason = failure;
  }
  return false;
}

bool hid_layout_parse(struct hid_report_layout *layout, const uint8_t *descriptor,
    size_t bytes, const char **reason)
{
  return parse(layout, descriptor, bytes, NULL, reason);
}

bool hid_layout_parse_touchpad(struct hid_report_layout *layout,
    const uint8_t *descriptor, size_t bytes, struct hid_touchpad_profile *profile,
    const char **reason)
{
  if (!profile) {
    if (layout) {
      memset(layout, 0, sizeof(*layout));
    }
    if (reason) {
      *reason = "missing touchpad profile storage";
    }
    return false;
  }
  return parse(layout, descriptor, bytes, profile, reason);
}
