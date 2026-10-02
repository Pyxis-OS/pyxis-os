#ifndef USB_BOT_H
#define USB_BOT_H

#include "host.h"

/* Checked descriptor facts, private to the core/class boundary. Endpoint
 * storage only covers the initial profile; larger shapes remain classified. */
struct usb_interface_description {
  uint8_t number, alternate, class, subclass, protocol, endpoint_count;
  struct {
    uint8_t attributes, companion_attributes;
    uint16_t bytes_per_interval;
    bool companion;
    struct usb_bulk_endpoint endpoint;
  } endpoints[2];
};

struct usb_bot_candidate {
  uint8_t configuration, interface, alternate;
  struct usb_bulk_endpoint endpoints[2];
};

bool usb_bot_match(enum usb_speed speed, uint8_t device_class,
                   uint8_t interface_count, uint8_t configuration,
                   const struct usb_interface_description *interface,
                   struct usb_bot_candidate *candidate);

#endif
