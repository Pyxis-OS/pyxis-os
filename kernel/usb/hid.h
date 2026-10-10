#ifndef USB_HID_H
#define USB_HID_H

#include <kernel/input.h>
#include "host.h"
#include "settings.h"

#define USB_HID_KEYBOARD_BYTES 8
#define USB_HID_MOUSE_BYTES 3

struct usb_hid_interface {
  struct usb_interrupt_endpoint endpoint;
  struct usb_host_interrupt *stream;
  uint16_t receive_bytes, descriptor_bytes;
  uint8_t number, protocol;
  bool qemu_wheel_candidate, qemu_wheel;
};

struct usb_hid_binding {
  struct input_source source;
  struct usb_host_device *host;
  struct usb_hid_interface interfaces[USB_HID_ENDPOINTS_PER_DEVICE];
  struct usb_ticket ticket;
  uint64_t deadline;
  enum usb_result result;
  unsigned stage, interface;
  uint8_t configuration;
  bool initial_keys[KEY_COUNT];
  unsigned initial_buttons;
  bool ticket_active, claimed, active, lost, retirement_pending;
};

/* The core validates the complete configuration before selecting its first
 * supported boot keyboard/mouse configuration. Existing class owners win. */
void usb_hid_select(struct usb_hid_binding *binding, const uint8_t *configuration,
                    size_t bytes, enum usb_speed speed, uint16_t vendor, uint16_t product);
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
