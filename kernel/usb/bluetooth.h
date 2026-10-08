#ifndef USB_BLUETOOTH_H
#define USB_BLUETOOTH_H

#include "host.h"

struct usb_bluetooth_binding {
  struct usb_interrupt_endpoint event;
  struct usb_bulk_endpoint acl_in, acl_out;
  uint8_t configuration;
  bool selected;
};

bool usb_bluetooth_ax200(uint16_t vendor, uint16_t product);
/* The USB core has checked the entire configuration before class selection. */
void usb_bluetooth_select(struct usb_bluetooth_binding *binding,
    const uint8_t *configuration, size_t bytes);
/* Owning BSP USB worker, boot enumeration only. No HCI command wait. */
enum usb_result usb_bluetooth_bind(struct usb_bluetooth_binding *binding,
    struct usb_host_controller *controller, struct usb_host_device *device,
    uint64_t deadline);

#endif
