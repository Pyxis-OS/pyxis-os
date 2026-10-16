/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/input/hid_layout.h>
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
#define HID_DATA_BUFFERED (1u << 8)
#define HID_COLLECTION_APPLICATION 1
#define HID_USAGE_CONFIGURATION 0x000d000e
#define HID_USAGE_INPUT_MODE 0x000d0052

struct hid_layout_global {
  uint32_t page, size, count;
  int64_t minimum, maximum;
  uint8_t id;
  bool have_minimum, have_maximum;
};

struct hid_layout_usage {
  /* Preserve usage ordering without expanding descriptor-supplied ranges. */
  uint32_t first, last;
};

struct hid_layout_parser {
  struct hid_layout_global global, globals[HID_LAYOUT_DEPTH];
  struct hid_layout_usage usages[HID_LAYOUT_USAGE_SPANS];
  bool configurations[HID_LAYOUT_DEPTH];
  unsigned global_depth, collection_depth, usage_count;
  bool range_open;
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
  case 3: case 4: case 5: case 6:
    return NULL; /* Physical range and units do not alter the bit layout. */
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

static const char *data_item(struct hid_layout_parser *parser,
    struct hid_report_layout *layout, enum hid_layout_type type, uint32_t flags,
    unsigned bytes)
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
  if (type == HID_LAYOUT_FEATURE && parser->configurations[parser->collection_depth - 1]) {
    return input_mode(parser, layout, offset, flags);
  }
  return NULL;
}

static const char *main_item(struct hid_layout_parser *parser,
    struct hid_report_layout *layout, unsigned tag, uint32_t value, unsigned bytes)
{
  if (parser->range_open) {
    return "unfinished usage range";
  }
  switch (tag) {
  case HID_MAIN_INPUT:
    return data_item(parser, layout, HID_LAYOUT_INPUT, value, bytes);
  case HID_MAIN_OUTPUT:
    return data_item(parser, layout, HID_LAYOUT_OUTPUT, value, bytes);
  case HID_MAIN_FEATURE:
    return data_item(parser, layout, HID_LAYOUT_FEATURE, value, bytes);
  case HID_MAIN_COLLECTION: {
    if (bytes != 1 || parser->collection_depth == HID_LAYOUT_DEPTH) {
      return "collection shape or nesting budget";
    }
    bool configuration = parser->collection_depth ?
      parser->configurations[parser->collection_depth - 1] :
      value == HID_COLLECTION_APPLICATION && parser->usage_count &&
        parser->usages[0].first == HID_USAGE_CONFIGURATION;
    parser->configurations[parser->collection_depth++] = configuration;
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

bool hid_layout_parse(struct hid_report_layout *layout, const uint8_t *descriptor,
    size_t bytes, const char **reason)
{
  const char *failure = NULL;
  if (reason) {
    *reason = NULL;
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
      failure = main_item(&parser, layout, tag, value, size);
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
  return true;

failed:
  if (layout) {
    memset(layout, 0, sizeof(*layout));
  }
  if (reason) {
    *reason = failure;
  }
  return false;
}
