#ifndef USB_HID_H
#define USB_HID_H

#include <kernel/input.h>
#include <kernel/gamepad.h>
#include "host.h"
#include "settings.h"
#include "hid_report.h"

#define USB_HID_KEYBOARD_BYTES 8
#define USB_HID_MOUSE_BYTES 3
/* One eligible interface needs an interface, HID and interrupt endpoint descriptor. */
#define USB_HID_MOUSE_CANDIDATES (USB_CONTROL_BYTES / (9 + 9 + 7))

struct usb_hid_interface {
  struct usb_interrupt_endpoint endpoint;
  struct usb_host_interrupt *stream;
  uint16_t receive_bytes, descriptor_bytes;
  uint64_t malformed_reports, malformed_diagnostic_ns;
  unsigned malformed_consecutive;
  unsigned mouse_samples;
  bool length_diagnosed[USB_HID_REPORT_LAYOUTS];
  uint8_t number, protocol;
  bool boot, report_protocol, reserved_id_boot;
  bool qemu_wheel_candidate, qemu_wheel, qemu_keyboard_candidate, qemu_keyboard;
};

struct usb_hid_binding {
  struct input_source source;
  struct gamepad_source gamepad;
  struct usb_host_device *host;
  struct usb_hid_interface interfaces[USB_HID_ENDPOINTS_PER_DEVICE];
  struct usb_hid_interface candidates[USB_HID_MOUSE_CANDIDATES];
  struct usb_hid_mouse_reports mouse;
  uint8_t control[USB_CONTROL_BYTES];
  struct usb_ticket ticket;
  uint64_t deadline;
  enum usb_result result;
  unsigned stage, interface, initial_observations, candidate_count, candidate, layout;
  uint8_t configuration;
  uint16_t vendor, product, version;
  bool initial_keys[KEY_COUNT];
  unsigned initial_buttons;
  bool ticket_active, claimed, active, lost, retirement_pending;
};

/* The core validates the complete configuration before selecting its first
 * eligible keyboard/mouse or dedicated Edge configuration. Report admission follows configuration;
 * a generic HID candidate does not establish that a mouse is supported.
 * Existing class owners win. All admission storage is retained in the record. */
void usb_hid_select(struct usb_hid_binding *binding, const uint8_t *configuration,
                    size_t bytes, enum usb_speed speed, uint16_t vendor, uint16_t product,
                    uint16_t version);
enum usb_result usb_hid_begin(struct usb_hid_binding *binding,
                              struct usb_host_device *device, uint64_t deadline);
/* One setup step or nonblocking EP0 collection per owning worker pass. */
enum usb_result usb_hid_bind_step(struct usb_hid_binding *binding);
enum usb_result usb_hid_bind_boot(struct usb_hid_binding *binding,
                                  struct usb_host_device *device, uint64_t deadline);
/* No waits, commands or allocation. Source callbacks briefly disable IF. */
void usb_hid_collect(struct usb_hid_binding *binding);
void usb_hid_lost(struct usb_hid_binding *binding);

#endif
