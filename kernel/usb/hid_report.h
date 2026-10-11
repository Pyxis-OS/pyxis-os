#ifndef USB_HID_REPORT_H
#define USB_HID_REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "settings.h"

/* Prepared admission budgets, not limits imposed by HID or the public ABI. */
#define USB_HID_REPORT_LAYOUTS 16
#define USB_HID_REPORT_FIELDS 64
#define USB_HID_REPORT_DEPTH 8
#define USB_HID_REPORT_USAGES 64
#define USB_HID_REPORT_SCOPES 64

enum usb_hid_report_type { HID_INPUT, HID_OUTPUT, HID_FEATURE, HID_REPORT_TYPES };
enum usb_hid_field_kind {
  HID_X, HID_Y, HID_WHEEL, HID_PAN, HID_BUTTON, HID_BUTTON_ARRAY, HID_MULTIPLIER,
};
enum usb_hid_decode_failure {
  HID_DECODE_EMPTY, HID_DECODE_ID, HID_DECODE_LENGTH, HID_DECODE_RANGE, HID_DECODE_ARRAY,
};

struct usb_hid_report_layout {
  uint32_t bits[HID_REPORT_TYPES];
  uint32_t buttons;
  uint8_t id;
  bool pointer, button_fields;
};

struct usb_hid_report_field {
  int64_t minimum, maximum, physical_minimum, physical_maximum;
  uint32_t offset, unit, multiplier;
  uint16_t scope, usage_start, usage_count;
  uint8_t layout, size, button;
  int8_t exponent;
  enum usb_hid_field_kind kind;
  bool null_state;
};

struct usb_hid_report_scope {
  uint16_t parent;
  unsigned controls;
};

struct usb_hid_report_global {
  int64_t minimum, maximum, physical_minimum, physical_maximum;
  uint32_t page, size, count, unit;
  uint8_t id;
  int8_t exponent;
};

struct usb_hid_report_collection {
  uint16_t scope;
  bool mouse, ignored;
};

struct usb_hid_report_parser {
  struct usb_hid_report_global global, globals[USB_HID_REPORT_DEPTH];
  struct usb_hid_report_collection collections[USB_HID_REPORT_DEPTH];
  uint32_t usages[USB_HID_REPORT_USAGES], usage_minimum;
  unsigned usage_count, collection_depth, global_depth, delimiter_branch;
  bool usage_range, delimiter_open, keyboard_usage;
};

struct usb_hid_mouse_reports {
  struct usb_hid_report_layout layouts[USB_HID_REPORT_LAYOUTS];
  struct usb_hid_report_field fields[USB_HID_REPORT_FIELDS];
  struct usb_hid_report_scope scopes[USB_HID_REPORT_SCOPES];
  uint32_t usages[USB_HID_REPORT_USAGES];
  struct usb_hid_report_parser parser;
  unsigned layout_count, field_count, scope_count, usage_count;
  uint32_t pan_multiplier, wheel_multiplier;
  uint16_t input_bytes;
  bool ids, wheel_enabled, pan_enabled, keyboard_data;
  /* Written only on a failed decode; no successful-report payload is retained. */
  enum usb_hid_decode_failure decode_failure;
  unsigned decode_field;
  int64_t decode_value;
};

struct usb_hid_mouse_sample {
  int32_t x, y, wheel, pan;
  uint32_t buttons;
};

bool usb_hid_mouse_parse(struct usb_hid_mouse_reports *mouse, const uint8_t *descriptor,
                         size_t bytes);
bool usb_hid_mouse_decode(struct usb_hid_mouse_reports *mouse, const uint8_t *report,
                          size_t bytes, struct usb_hid_mouse_sample *sample);
size_t usb_hid_report_bytes(const struct usb_hid_mouse_reports *mouse, unsigned layout,
                            enum usb_hid_report_type type);
uint32_t usb_hid_mouse_buttons(const struct usb_hid_mouse_reports *mouse);
bool usb_hid_feature_layout(const struct usb_hid_mouse_reports *mouse, unsigned layout);
/* A complete Feature image is read before modification. Only admitted scalar
 * fields are changed; all padding and unrelated Feature fields are preserved. */
bool usb_hid_feature_prepare(const struct usb_hid_mouse_reports *mouse, unsigned layout,
                             uint8_t *report, size_t bytes);
bool usb_hid_feature_confirm(struct usb_hid_mouse_reports *mouse, unsigned layout,
                             const uint8_t *report, size_t bytes,
                             uint32_t *pan_multiplier, uint32_t *wheel_multiplier);
void usb_hid_feature_disable(struct usb_hid_mouse_reports *mouse, unsigned layout);

#endif
