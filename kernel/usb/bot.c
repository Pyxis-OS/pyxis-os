#include "bot.h"

#define USB_CLASS_INTERFACE 0x00
#define USB_CLASS_STORAGE 0x08
#define USB_STORAGE_SCSI 0x06
#define USB_STORAGE_BULK_ONLY 0x50
#define USB_ENDPOINT_TYPE_MASK 0x03
#define USB_ENDPOINT_BULK 0x02
#define USB_ENDPOINT_IN 0x80
#define USB_SUPER_BULK_PACKET 1024
#define USB_HIGH_BULK_PACKET 512
#define USB_SUPER_MAX_BURST 15

bool usb_bot_match(enum usb_speed speed, uint8_t device_class,
                   uint8_t interface_count, uint8_t configuration,
                   const struct usb_interface_description *interface,
                   struct usb_bot_candidate *candidate)
{
  if ((device_class != USB_CLASS_INTERFACE && device_class != USB_CLASS_STORAGE) ||
      interface_count != 1 || interface->class != USB_CLASS_STORAGE ||
      interface->subclass != USB_STORAGE_SCSI || interface->protocol != USB_STORAGE_BULK_ONLY ||
      interface->endpoint_count != 2) {
    return false;
  }

  if ((interface->endpoints[0].endpoint.address & USB_ENDPOINT_IN) ==
      (interface->endpoints[1].endpoint.address & USB_ENDPOINT_IN)) {
    return false;
  }

  for (unsigned i = 0; i < 2; ++i) {
    const struct usb_bulk_endpoint *endpoint = &interface->endpoints[i].endpoint;
    if ((interface->endpoints[i].attributes & USB_ENDPOINT_TYPE_MASK) != USB_ENDPOINT_BULK) {
      return false;
    }
    if (speed == USB_SPEED_SUPER) {
      if (endpoint->max_packet != USB_SUPER_BULK_PACKET ||
          !interface->endpoints[i].companion || endpoint->max_burst > USB_SUPER_MAX_BURST ||
          interface->endpoints[i].companion_attributes || interface->endpoints[i].bytes_per_interval) {
        return false;
      }
    } else if (speed == USB_SPEED_HIGH) {
      if (endpoint->max_packet != USB_HIGH_BULK_PACKET) {
        return false;
      }
    } else if (speed == USB_SPEED_FULL) {
      if (endpoint->max_packet != 8 && endpoint->max_packet != 16 &&
          endpoint->max_packet != 32 && endpoint->max_packet != 64) {
        return false;
      }
    } else {
      return false;
    }
  }

  candidate->configuration = configuration;
  candidate->interface = interface->number;
  candidate->alternate = interface->alternate;
  for (unsigned i = 0; i < 2; ++i) {
    candidate->endpoints[i] = interface->endpoints[i].endpoint;
  }
  return true;
}
