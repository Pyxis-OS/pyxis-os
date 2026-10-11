#include <kernel/memory.h>
#include "hid_report.h"

#define HID_ITEM_LONG 0xfe
#define HID_ITEM_MAIN 0
#define HID_ITEM_GLOBAL 1
#define HID_ITEM_LOCAL 2
#define HID_MAIN_INPUT 8
#define HID_MAIN_OUTPUT 9
#define HID_MAIN_COLLECTION 10
#define HID_MAIN_FEATURE 11
#define HID_MAIN_END_COLLECTION 12
#define HID_DATA_CONSTANT 0x001
#define HID_DATA_VARIABLE 0x002
#define HID_DATA_RELATIVE 0x004
#define HID_DATA_WRAP 0x008
#define HID_DATA_NONLINEAR 0x010
#define HID_DATA_NULL 0x040
#define HID_DATA_BUFFERED 0x100
#define HID_COLLECTION_APPLICATION 1
#define HID_COLLECTION_LOGICAL 2
#define HID_PAGE_DESKTOP 1
#define HID_PAGE_BUTTON 9
#define HID_PAGE_CONSUMER 12
#define HID_USAGE_MOUSE 2
#define HID_USAGE_X 0x30
#define HID_USAGE_Y 0x31
#define HID_USAGE_WHEEL 0x38
#define HID_USAGE_MULTIPLIER 0x48
#define HID_USAGE_PAN 0x238
#define HID_SCOPE_WHEEL 1
#define HID_SCOPE_PAN 2
#define HID_SCOPE_OTHER 4

static uint32_t item_value(const uint8_t *data, unsigned bytes)
{
  uint32_t value = 0;
  for (unsigned index = 0; index < bytes; ++index) {
    value |= (uint32_t)data[index] << (index * 8);
  }
  return value;
}

static int64_t signed_value(uint32_t value, unsigned bits)
{
  if (bits && bits < 32 && (value & (1u << (bits - 1)))) {
    return (int64_t)value - (1LL << bits);
  }
  return bits == 32 ? (int64_t)(int32_t)value : (int64_t)value;
}

static uint32_t usage_value(const struct usb_hid_report_parser *parser, uint32_t value,
                             unsigned bytes)
{
  return bytes == 4 ? value : (parser->global.page << 16) | value;
}

static bool local_controls_ignored(const struct usb_hid_report_parser *parser)
{
  if (!parser->collection_depth) {
    return false;
  }
  const struct usb_hid_report_collection *collection =
    &parser->collections[parser->collection_depth - 1];
  return !collection->mouse || collection->ignored;
}

static bool local_usage(struct usb_hid_report_parser *parser, uint32_t usage)
{
  if (parser->delimiter_open && parser->delimiter_branch > 1) {
    return true;
  }
  /* The first usage can name a nested Collection; ignored controls need no table. */
  if (local_controls_ignored(parser) && parser->usage_count) {
    return true;
  }
  if (parser->usage_count == USB_HID_REPORT_USAGES) {
    return false;
  }
  parser->usages[parser->usage_count++] = usage;
  return true;
}

static bool local_item(struct usb_hid_report_parser *parser, unsigned tag,
                        uint32_t value, unsigned bytes)
{
  switch (tag) {
  case 0:
    return local_usage(parser, usage_value(parser, value, bytes));
  case 1:
    if (parser->usage_range) {
      return false;
    }
    parser->usage_minimum = usage_value(parser, value, bytes);
    parser->usage_range = true;
    return true;
  case 2: {
    uint32_t last = usage_value(parser, value, bytes);
    uint32_t first = parser->usage_minimum;
    if (!parser->usage_range || first > last || (first >> 16) != (last >> 16)) {
      return false;
    }
    parser->usage_range = false;
    if (local_controls_ignored(parser)) {
      return local_usage(parser, first);
    }
    if ((uint64_t)last - first + 1 > USB_HID_REPORT_USAGES) {
      return false;
    }
    for (uint32_t usage = first; ; ++usage) {
      if (!local_usage(parser, usage)) {
        return false;
      }
      if (usage == last) {
        break;
      }
    }
    return true;
  }
  case 3: case 4: case 5: case 7: case 8: case 9:
    return true; /* Designator and string metadata do not change data layout. */
  case 10:
    if (value == 1 && !parser->delimiter_open) {
      parser->delimiter_open = true;
      ++parser->delimiter_branch;
      return true;
    }
    if (!value && parser->delimiter_open) {
      parser->delimiter_open = false;
      return true;
    }
    return false;
  default:
    return false;
  }
}

static bool global_item(struct usb_hid_mouse_reports *mouse, unsigned tag,
                         uint32_t value, unsigned bytes)
{
  struct usb_hid_report_parser *parser = &mouse->parser;
  struct usb_hid_report_global *global = &parser->global;
  switch (tag) {
  case 0:
    if (value > UINT16_MAX) {
      return false;
    }
    global->page = value;
    break;
  case 1: global->minimum = signed_value(value, bytes * 8); break;
  case 2:
    global->maximum = global->minimum < 0 ? signed_value(value, bytes * 8) : value;
    break;
  case 3: global->physical_minimum = signed_value(value, bytes * 8); break;
  case 4:
    global->physical_maximum = global->physical_minimum < 0 ?
      signed_value(value, bytes * 8) : value;
    break;
  case 5:
    /* HID encodes the exponent as a signed four-bit value. */
    if (value > 15 && (signed_value(value, bytes * 8) < -8 ||
                      signed_value(value, bytes * 8) >= 0)) {
      return false;
    }
    global->exponent = (value & 8) ? (int)(value & 15) - 16 : (int)(value & 7);
    break;
  case 6: global->unit = value; break;
  case 7: global->size = value; break;
  case 8:
    if (!value || value > UINT8_MAX) {
      return false;
    }
    for (unsigned index = 0; index < mouse->layout_count; ++index) {
      if (!mouse->layouts[index].id) {
        return false;
      }
    }
    global->id = value;
    mouse->ids = true;
    break;
  case 9: global->count = value; break;
  case 10:
    if (bytes || parser->global_depth == USB_HID_REPORT_DEPTH) {
      return false;
    }
    parser->globals[parser->global_depth++] = *global;
    break;
  case 11:
    if (bytes || !parser->global_depth) {
      return false;
    }
    *global = parser->globals[--parser->global_depth];
    break;
  default:
    return false;
  }
  return true;
}

static int report_layout(struct usb_hid_mouse_reports *mouse, uint8_t id)
{
  for (unsigned index = 0; index < mouse->layout_count; ++index) {
    if (mouse->layouts[index].id == id) {
      return index;
    }
  }
  if (mouse->layout_count == USB_HID_REPORT_LAYOUTS || (mouse->ids && !id)) {
    return -1;
  }
  unsigned index = mouse->layout_count++;
  mouse->layouts[index].id = id;
  return index;
}

static uint32_t control_usage(const struct usb_hid_report_parser *parser, unsigned index)
{
  if (!parser->usage_count) {
    return 0;
  }
  if (index >= parser->usage_count) {
    index = parser->usage_count - 1;
  }
  return parser->usages[index];
}

static bool collection_item(struct usb_hid_mouse_reports *mouse, uint32_t type,
                             unsigned *applications)
{
  struct usb_hid_report_parser *parser = &mouse->parser;
  if (parser->collection_depth == USB_HID_REPORT_DEPTH) {
    return false;
  }
  struct usb_hid_report_collection collection = {0};
  if (parser->collection_depth) {
    collection = parser->collections[parser->collection_depth - 1];
  } else if (type != HID_COLLECTION_APPLICATION) {
    return false;
  }
  uint32_t usage = control_usage(parser, 0);
  if (type == HID_COLLECTION_APPLICATION) {
    /* A second Mouse application is outside the single-pointer profile. */
    collection.mouse = usage == ((HID_PAGE_DESKTOP << 16) | HID_USAGE_MOUSE);
    collection.ignored = !collection.mouse;
    if (collection.mouse && ++*applications != 1) {
      return false;
    }
  } else if (type >= 0x80) {
    collection.ignored = true;
  }
  if (type == HID_COLLECTION_LOGICAL) {
    if (mouse->scope_count == USB_HID_REPORT_SCOPES) {
      return false;
    }
    unsigned scope = mouse->scope_count++;
    mouse->scopes[scope].parent = collection.scope;
    collection.scope = scope;
  }
  parser->collections[parser->collection_depth++] = collection;
  return true;
}

static void scope_control(struct usb_hid_mouse_reports *mouse, unsigned scope, unsigned controls)
{
  for (;;) {
    mouse->scopes[scope].controls |= controls;
    if (!scope) {
      break;
    }
    scope = mouse->scopes[scope].parent;
  }
}

static bool field_range(const struct usb_hid_report_global *global)
{
  if (!global->size || global->size > 32 || global->minimum > global->maximum) {
    return false;
  }
  int64_t minimum = global->minimum < 0 ? -(1LL << (global->size - 1)) : 0;
  int64_t maximum = global->minimum < 0 ? (1LL << (global->size - 1)) - 1 :
    (1LL << global->size) - 1;
  return global->minimum >= minimum && global->maximum <= maximum;
}

static bool append_field(struct usb_hid_mouse_reports *mouse, unsigned layout,
                          uint32_t offset, enum usb_hid_field_kind kind,
                          uint8_t button, uint16_t usage_start, uint16_t usage_count,
                          unsigned flags)
{
  const struct usb_hid_report_parser *parser = &mouse->parser;
  const struct usb_hid_report_global *global = &parser->global;
  if (mouse->field_count == USB_HID_REPORT_FIELDS || !field_range(global) ||
      (flags & (HID_DATA_WRAP | HID_DATA_NONLINEAR | HID_DATA_BUFFERED))) {
    return false;
  }
  mouse->fields[mouse->field_count++] = (struct usb_hid_report_field){
    .minimum = global->minimum, .maximum = global->maximum,
    .physical_minimum = global->physical_minimum, .physical_maximum = global->physical_maximum,
    .offset = offset, .unit = global->unit,
    .scope = parser->collections[parser->collection_depth - 1].scope,
    .usage_start = usage_start, .usage_count = usage_count,
    .layout = layout, .size = global->size, .button = button,
    .exponent = global->exponent, .kind = kind, .null_state = flags & HID_DATA_NULL,
  };
  if (kind != HID_MULTIPLIER) {
    mouse->layouts[layout].pointer = true;
  }
  if (kind == HID_BUTTON || kind == HID_BUTTON_ARRAY) {
    mouse->layouts[layout].button_fields = true;
  }
  return true;
}

static bool data_item(struct usb_hid_mouse_reports *mouse, enum usb_hid_report_type type,
                       unsigned flags, bool *have_x, bool *have_y)
{
  struct usb_hid_report_parser *parser = &mouse->parser;
  const struct usb_hid_report_global *global = &parser->global;
  if (!parser->collection_depth || !global->size || !global->count) {
    return false;
  }
  int layout = report_layout(mouse, global->id);
  if (layout < 0) {
    return false;
  }
  uint64_t bits = (uint64_t)global->size * global->count;
  uint32_t offset = mouse->layouts[layout].bits[type];
  unsigned capacity = type == HID_INPUT ? USB_HID_INTERRUPT_BYTES : USB_CONTROL_BYTES;
  if (bits > capacity * 8u || (uint64_t)offset + bits + (mouse->ids ? 8 : 0) > capacity * 8u) {
    return false;
  }
  mouse->layouts[layout].bits[type] += bits;
  const struct usb_hid_report_collection *collection =
    &parser->collections[parser->collection_depth - 1];
  bool relevant = collection->mouse && !collection->ignored;
  if (relevant) {
    for (unsigned index = 0; index < parser->usage_count; ++index) {
      if (parser->usages[index] == ((HID_PAGE_DESKTOP << 16) | HID_USAGE_MULTIPLIER) &&
          (type != HID_FEATURE || !(flags & HID_DATA_VARIABLE) || (flags & HID_DATA_CONSTANT))) {
        return false;
      }
    }
  }
  if (flags & HID_DATA_CONSTANT) {
    return true;
  }
  if (type == HID_OUTPUT) {
    scope_control(mouse, collection->scope, HID_SCOPE_OTHER);
    return true;
  }
  if (!(flags & HID_DATA_VARIABLE)) {
    if (type == HID_INPUT) {
      scope_control(mouse, collection->scope, HID_SCOPE_OTHER);
    }
    if (type != HID_INPUT || !relevant) {
      if (type == HID_FEATURE) {
        scope_control(mouse, collection->scope, HID_SCOPE_OTHER);
      }
      return true;
    }
    if (global->count > USB_HID_REPORT_FIELDS || !parser->usage_count) {
      return false;
    }
    bool buttons = false;
    for (unsigned index = 0; index < parser->usage_count; ++index) {
      if ((parser->usages[index] >> 16) != HID_PAGE_BUTTON) {
        scope_control(mouse, collection->scope, HID_SCOPE_OTHER);
        return true;
      }
      buttons |= (parser->usages[index] & UINT16_MAX) >= 1 &&
        (parser->usages[index] & UINT16_MAX) <= 32;
    }
    scope_control(mouse, collection->scope, HID_SCOPE_OTHER);
    if (!buttons) {
      return true;
    }
    if ((flags & HID_DATA_RELATIVE) ||
        parser->usage_count > USB_HID_REPORT_USAGES - mouse->usage_count) {
      return false;
    }
    unsigned start = mouse->usage_count;
    for (unsigned index = 0; index < parser->usage_count; ++index) {
      mouse->usages[mouse->usage_count++] = parser->usages[index];
    }
    for (unsigned index = 0; index < global->count; ++index) {
      if (!append_field(mouse, layout, offset + index * global->size, HID_BUTTON_ARRAY,
                        0, start, parser->usage_count, flags)) {
        return false;
      }
    }
    return true;
  }
  /* A huge ignored variable item advances offsets without expanding usages. */
  if (!relevant) {
    scope_control(mouse, collection->scope, HID_SCOPE_OTHER);
    return true;
  }
  for (unsigned index = 0; index < global->count; ++index) {
    uint32_t usage = control_usage(parser, index);
    unsigned page = usage >> 16, code = usage & UINT16_MAX;
    enum usb_hid_field_kind kind;
    if (type == HID_FEATURE) {
      if (page != HID_PAGE_DESKTOP || code != HID_USAGE_MULTIPLIER) {
        scope_control(mouse, collection->scope, HID_SCOPE_OTHER);
        continue;
      }
      if ((flags & HID_DATA_RELATIVE) || !collection->scope || global->unit) {
        return false;
      }
      kind = HID_MULTIPLIER;
    } else if (page == HID_PAGE_DESKTOP && code == HID_USAGE_X) {
      kind = HID_X;
      *have_x = true;
    } else if (page == HID_PAGE_DESKTOP && code == HID_USAGE_Y) {
      kind = HID_Y;
      *have_y = true;
    } else if (page == HID_PAGE_DESKTOP && code == HID_USAGE_WHEEL) {
      kind = HID_WHEEL;
    } else if (page == HID_PAGE_CONSUMER && code == HID_USAGE_PAN) {
      kind = HID_PAN;
    } else if (page == HID_PAGE_BUTTON && code >= 1 && code <= 32) {
      kind = HID_BUTTON;
    } else {
      scope_control(mouse, collection->scope, HID_SCOPE_OTHER);
      continue;
    }
    if (type == HID_INPUT) {
      unsigned controls = kind == HID_WHEEL ? HID_SCOPE_WHEEL :
        kind == HID_PAN ? HID_SCOPE_PAN : HID_SCOPE_OTHER;
      scope_control(mouse, collection->scope, controls);
      if (kind == HID_BUTTON) {
        if ((flags & HID_DATA_RELATIVE) || global->minimum != 0 || global->maximum != 1) {
          return false;
        }
      } else if (!(flags & HID_DATA_RELATIVE) || global->minimum >= 0 ||
                 global->maximum > INT32_MAX) {
        return false;
      }
    }
    if (!append_field(mouse, layout, offset + index * global->size, kind,
                      kind == HID_BUTTON ? code : 0, 0, 0, flags)) {
      return false;
    }
  }
  return true;
}

size_t usb_hid_report_bytes(const struct usb_hid_mouse_reports *mouse, unsigned layout,
                            enum usb_hid_report_type type)
{
  uint32_t bits = mouse->layouts[layout].bits[type];
  return bits ? (bits + 7) / 8 + (mouse->ids ? 1 : 0) : 0;
}

static bool in_scope(const struct usb_hid_mouse_reports *mouse, unsigned field_scope,
                      unsigned multiplier_scope)
{
  while (field_scope) {
    if (field_scope == multiplier_scope) {
      return true;
    }
    field_scope = mouse->scopes[field_scope].parent;
  }
  return false;
}

static void update_multipliers(struct usb_hid_mouse_reports *mouse)
{
  mouse->pan_multiplier = mouse->wheel_multiplier = 1;
  mouse->pan_enabled = mouse->wheel_enabled = true;
  bool pan_seen = false, wheel_seen = false;
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    const struct usb_hid_report_field *field = &mouse->fields[index];
    if (field->kind != HID_MULTIPLIER) {
      continue;
    }
    unsigned controls = mouse->scopes[field->scope].controls;
    if (controls & HID_SCOPE_PAN) {
      if (!field->multiplier || (pan_seen && mouse->pan_multiplier != field->multiplier)) {
        mouse->pan_enabled = false;
      }
      mouse->pan_multiplier = field->multiplier;
      pan_seen = true;
    }
    if (controls & HID_SCOPE_WHEEL) {
      if (!field->multiplier || (wheel_seen && mouse->wheel_multiplier != field->multiplier)) {
        mouse->wheel_enabled = false;
      }
      mouse->wheel_multiplier = field->multiplier;
      wheel_seen = true;
    }
  }
  /* One source-axis scale must also cover any wheel outside multiplier scopes. */
  bool have_pan = false, have_wheel = false;
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    const struct usb_hid_report_field *axis = &mouse->fields[index];
    if (axis->kind != HID_WHEEL && axis->kind != HID_PAN) {
      continue;
    }
    have_pan |= axis->kind == HID_PAN;
    have_wheel |= axis->kind == HID_WHEEL;
    bool covered = false;
    for (unsigned multiplier = 0; multiplier < mouse->field_count; ++multiplier) {
      const struct usb_hid_report_field *field = &mouse->fields[multiplier];
      covered |= field->kind == HID_MULTIPLIER && in_scope(mouse, axis->scope, field->scope);
    }
    if (!covered && axis->kind == HID_WHEEL && mouse->wheel_multiplier != 1) {
      mouse->wheel_enabled = false;
    }
    if (!covered && axis->kind == HID_PAN && mouse->pan_multiplier != 1) {
      mouse->pan_enabled = false;
    }
  }
  mouse->pan_enabled &= have_pan;
  mouse->wheel_enabled &= have_wheel;
  if (!mouse->pan_enabled) {
    mouse->pan_multiplier = 1;
  }
  if (!mouse->wheel_enabled) {
    mouse->wheel_multiplier = 1;
  }
}

bool usb_hid_mouse_parse(struct usb_hid_mouse_reports *mouse, const uint8_t *descriptor,
                         size_t bytes)
{
  memset(mouse, 0, sizeof(*mouse));
  if (!bytes || bytes > USB_CONTROL_BYTES) {
    return false;
  }
  mouse->scope_count = 1;
  unsigned applications = 0;
  bool have_x = false, have_y = false;
  for (size_t offset = 0; offset < bytes;) {
    uint8_t prefix = descriptor[offset++];
    unsigned size = prefix & 3;
    if (size == 3) {
      size = 4;
    }
    if (prefix == HID_ITEM_LONG || size > bytes - offset) {
      return false;
    }
    uint32_t value = item_value(descriptor + offset, size);
    offset += size;
    unsigned tag = prefix >> 4, type = (prefix >> 2) & 3;
    struct usb_hid_report_parser *parser = &mouse->parser;
    if (type == HID_ITEM_GLOBAL) {
      if (!global_item(mouse, tag, value, size)) {
        return false;
      }
    } else if (type == HID_ITEM_LOCAL) {
      if (!local_item(parser, tag, value, size)) {
        return false;
      }
    } else if (type == HID_ITEM_MAIN) {
      if (parser->usage_range || parser->delimiter_open) {
        return false;
      }
      bool valid;
      switch (tag) {
      case HID_MAIN_INPUT:
        valid = data_item(mouse, HID_INPUT, value, &have_x, &have_y);
        break;
      case HID_MAIN_OUTPUT:
        valid = data_item(mouse, HID_OUTPUT, value, &have_x, &have_y);
        break;
      case HID_MAIN_FEATURE:
        valid = data_item(mouse, HID_FEATURE, value, &have_x, &have_y);
        break;
      case HID_MAIN_COLLECTION:
        valid = collection_item(mouse, value, &applications);
        break;
      case HID_MAIN_END_COLLECTION:
        valid = !size && parser->collection_depth;
        if (valid) {
          --parser->collection_depth;
        }
        break;
      default: valid = false; break;
      }
      if (!valid) {
        return false;
      }
      parser->usage_count = parser->delimiter_branch = 0;
    } else {
      return false;
    }
  }
  if (mouse->parser.collection_depth || mouse->parser.global_depth ||
      mouse->parser.usage_range || mouse->parser.delimiter_open || !applications ||
      !have_x || !have_y) {
    return false;
  }
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    const struct usb_hid_report_field *field = &mouse->fields[index];
    if (field->kind == HID_MULTIPLIER) {
      unsigned controls = mouse->scopes[field->scope].controls;
      if ((controls & HID_SCOPE_OTHER) || !(controls & (HID_SCOPE_WHEEL | HID_SCOPE_PAN))) {
        return false;
      }
    }
  }
  for (unsigned index = 0; index < mouse->layout_count; ++index) {
    size_t input = usb_hid_report_bytes(mouse, index, HID_INPUT);
    if (input > mouse->input_bytes) {
      mouse->input_bytes = input;
    }
  }
  update_multipliers(mouse);
  return mouse->input_bytes != 0;
}

static uint32_t field_bits(const struct usb_hid_report_field *field, const uint8_t *report)
{
  uint32_t value = 0;
  for (unsigned bit = 0; bit < field->size; ++bit) {
    unsigned offset = field->offset + bit;
    if (report[offset / 8] & (1u << (offset % 8))) {
      value |= 1u << bit;
    }
  }
  return value;
}

static int64_t field_value(const struct usb_hid_report_field *field, const uint8_t *report)
{
  uint32_t value = field_bits(field, report);
  return field->minimum < 0 ? signed_value(value, field->size) : value;
}

static int32_t saturate(int64_t value)
{
  return value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : value;
}

uint32_t usb_hid_mouse_buttons(const struct usb_hid_mouse_reports *mouse)
{
  uint32_t buttons = 0;
  for (unsigned index = 0; index < mouse->layout_count; ++index) {
    buttons |= mouse->layouts[index].buttons;
  }
  return buttons;
}

static bool decode_failed(struct usb_hid_mouse_reports *mouse,
    enum usb_hid_decode_failure failure, unsigned field, int64_t value)
{
  mouse->decode_failure = failure;
  mouse->decode_field = field;
  mouse->decode_value = value;
  return false;
}

bool usb_hid_mouse_decode(struct usb_hid_mouse_reports *mouse, const uint8_t *report,
                          size_t bytes, struct usb_hid_mouse_sample *sample)
{
  if (!bytes) {
    return decode_failed(mouse, HID_DECODE_EMPTY, 0, 0);
  }
  unsigned layout = 0;
  uint8_t id = mouse->ids ? report[0] : 0;
  while (layout < mouse->layout_count && mouse->layouts[layout].id != id) {
    ++layout;
  }
  if (layout == mouse->layout_count) {
    return decode_failed(mouse, HID_DECODE_ID, 0, 0);
  }
  if (bytes != usb_hid_report_bytes(mouse, layout, HID_INPUT)) {
    return decode_failed(mouse, HID_DECODE_LENGTH, 0, 0);
  }
  const uint8_t *payload = report + (mouse->ids ? 1 : 0);
  int64_t x = 0, y = 0, wheel = 0, pan = 0;
  uint32_t buttons = 0;
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    const struct usb_hid_report_field *field = &mouse->fields[index];
    if (field->layout != layout || field->kind == HID_MULTIPLIER) {
      continue;
    }
    int64_t value = field_value(field, payload);
    if (value < field->minimum || value > field->maximum) {
      if (field->null_state) {
        continue;
      }
      return decode_failed(mouse, HID_DECODE_RANGE, index, value);
    }
    switch (field->kind) {
    case HID_X: x += value; break;
    case HID_Y: y += value; break;
    case HID_WHEEL: wheel -= value; break;
    case HID_PAN: pan += value; break;
    case HID_BUTTON:
      if (value) {
        buttons |= 1u << (field->button - 1);
      }
      break;
    case HID_BUTTON_ARRAY: {
      uint64_t usage_index = value - field->minimum;
      if (usage_index >= field->usage_count) {
        if (field->null_state) {
          break;
        }
        return decode_failed(mouse, HID_DECODE_ARRAY, index, value);
      }
      unsigned button = mouse->usages[field->usage_start + usage_index] & UINT16_MAX;
      if (button >= 1 && button <= 32) {
        buttons |= 1u << (button - 1);
      }
      break;
    }
    case HID_MULTIPLIER: break;
    }
  }
  if (mouse->layouts[layout].button_fields) {
    mouse->layouts[layout].buttons = buttons;
  }
  *sample = (struct usb_hid_mouse_sample){
    .x = saturate(x), .y = saturate(y),
    .wheel = mouse->wheel_enabled ? saturate(wheel) : 0,
    .pan = mouse->pan_enabled ? saturate(pan) : 0,
    .buttons = usb_hid_mouse_buttons(mouse),
  };
  return true;
}

bool usb_hid_feature_layout(const struct usb_hid_mouse_reports *mouse, unsigned layout)
{
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    if (mouse->fields[index].layout == layout && mouse->fields[index].kind == HID_MULTIPLIER) {
      return true;
    }
  }
  return false;
}

static bool feature_image(const struct usb_hid_mouse_reports *mouse, unsigned layout,
                           const uint8_t *report, size_t bytes)
{
  return bytes == usb_hid_report_bytes(mouse, layout, HID_FEATURE) &&
    (!mouse->ids || (bytes && report[0] == mouse->layouts[layout].id));
}

static uint32_t effective_multiplier(const struct usb_hid_report_field *field, int64_t value)
{
  if (value < field->minimum || value > field->maximum || field->minimum == field->maximum) {
    return 0;
  }
  int64_t numerator, part, denominator = field->maximum - field->minimum;
  if (__builtin_mul_overflow(value - field->minimum,
                            field->physical_maximum - field->physical_minimum, &part) ||
      __builtin_mul_overflow(field->physical_minimum, denominator, &numerator) ||
      __builtin_add_overflow(numerator, part, &numerator)) {
    return 0;
  }
  for (int exponent = field->exponent; exponent > 0; --exponent) {
    if (__builtin_mul_overflow(numerator, 10LL, &numerator)) {
      return 0;
    }
  }
  for (int exponent = field->exponent; exponent < 0; ++exponent) {
    if (__builtin_mul_overflow(denominator, 10LL, &denominator)) {
      return 0;
    }
  }
  if (numerator <= 0 || numerator % denominator || numerator / denominator > INT32_MAX) {
    return 0;
  }
  return numerator / denominator;
}

static void field_write(const struct usb_hid_report_field *field, uint8_t *report, int64_t value)
{
  for (unsigned bit = 0; bit < field->size; ++bit) {
    unsigned offset = field->offset + bit;
    uint8_t mask = 1u << (offset % 8);
    if ((uint32_t)value & (1u << bit)) {
      report[offset / 8] |= mask;
    } else {
      report[offset / 8] &= ~mask;
    }
  }
}

bool usb_hid_feature_prepare(const struct usb_hid_mouse_reports *mouse, unsigned layout,
                             uint8_t *report, size_t bytes)
{
  if (!feature_image(mouse, layout, report, bytes)) {
    return false;
  }
  /* Validate every selected field before changing any bit of the complete image. */
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    const struct usb_hid_report_field *field = &mouse->fields[index];
    if (field->layout == layout && field->kind == HID_MULTIPLIER &&
        !effective_multiplier(field, field->minimum) &&
        !effective_multiplier(field, field->maximum)) {
      return false;
    }
  }
  uint8_t *payload = report + (mouse->ids ? 1 : 0);
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    const struct usb_hid_report_field *field = &mouse->fields[index];
    if (field->layout == layout && field->kind == HID_MULTIPLIER) {
      uint32_t minimum = effective_multiplier(field, field->minimum);
      uint32_t maximum = effective_multiplier(field, field->maximum);
      field_write(field, payload, maximum >= minimum ? field->maximum : field->minimum);
    }
  }
  return true;
}

bool usb_hid_feature_confirm(struct usb_hid_mouse_reports *mouse, unsigned layout,
                             const uint8_t *report, size_t bytes,
                             uint32_t *pan_multiplier, uint32_t *wheel_multiplier)
{
  if (!feature_image(mouse, layout, report, bytes)) {
    return false;
  }
  const uint8_t *payload = report + (mouse->ids ? 1 : 0);
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    const struct usb_hid_report_field *field = &mouse->fields[index];
    if (field->layout == layout && field->kind == HID_MULTIPLIER &&
        !effective_multiplier(field, field_value(field, payload))) {
      return false;
    }
  }
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    struct usb_hid_report_field *field = &mouse->fields[index];
    if (field->layout == layout && field->kind == HID_MULTIPLIER) {
      field->multiplier = effective_multiplier(field, field_value(field, payload));
    }
  }
  update_multipliers(mouse);
  *pan_multiplier = mouse->pan_multiplier;
  *wheel_multiplier = mouse->wheel_multiplier;
  return true;
}

void usb_hid_feature_disable(struct usb_hid_mouse_reports *mouse, unsigned layout)
{
  for (unsigned index = 0; index < mouse->field_count; ++index) {
    struct usb_hid_report_field *field = &mouse->fields[index];
    if (field->layout == layout && field->kind == HID_MULTIPLIER) {
      field->multiplier = 0;
    }
  }
  update_multipliers(mouse);
}
