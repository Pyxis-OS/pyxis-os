#include <abi/bluetooth_hci.h>
#include <kernel/bluetooth.h>
#include <kernel/memory.h>
#include "bluetooth.h"

#define AX200_VENDOR 0x8087
#define AX200_PRODUCT 0x0029
#define AX200_INTERFACE 0
#define AX200_CONFIGURATION 1
#define AX200_ENDPOINTS 3
#define AX200_PACKET 64
#define AX200_EVENT_INTERVAL 1
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT 5
#define USB_CONFIGURATION_BYTES 9
#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_TYPE_MASK 0x03
#define USB_ENDPOINT_BULK 2
#define USB_ENDPOINT_INTERRUPT 3
#define USB_CLASS_WIRELESS 0xe0
#define USB_SUBCLASS_RF 1
#define USB_PROTOCOL_BLUETOOTH 1
#define USB_REQUEST_DEVICE_OUT 0
#define USB_REQUEST_SET_CONFIGURATION 9
#define HCI_EVENT_BYTES (2 + UINT8_MAX)

bool usb_bluetooth_ax200(uint16_t vendor, uint16_t product)
{
  return vendor == AX200_VENDOR && product == AX200_PRODUCT;
}

void usb_bluetooth_select(struct usb_bluetooth_binding *binding,
    const uint8_t *configuration, size_t bytes)
{
  if (configuration[5] != AX200_CONFIGURATION) {
    return;
  }
  struct usb_bluetooth_binding candidate = {.configuration = configuration[5]};
  bool hci = false, valid = true;
  unsigned endpoints = 0;
  for (size_t offset = USB_CONFIGURATION_BYTES; offset < bytes;
       offset += configuration[offset]) {
    const uint8_t *part = configuration + offset;
    if (part[1] == USB_DESCRIPTOR_INTERFACE) {
      hci = part[2] == AX200_INTERFACE && !part[3] &&
        part[4] == AX200_ENDPOINTS && part[5] == USB_CLASS_WIRELESS &&
        part[6] == USB_SUBCLASS_RF && part[7] == USB_PROTOCOL_BLUETOOTH;
    } else if (hci && part[1] == USB_DESCRIPTOR_ENDPOINT) {
      uint16_t packet = part[4] | ((uint16_t)part[5] << 8);
      unsigned type = part[3] & USB_ENDPOINT_TYPE_MASK;
      ++endpoints;
      if (packet != AX200_PACKET) {
        valid = false;
      }
      if (type == USB_ENDPOINT_INTERRUPT && (part[2] & USB_ENDPOINT_IN) &&
          part[6] == AX200_EVENT_INTERVAL && !candidate.event.address) {
        candidate.event = (struct usb_interrupt_endpoint){
          .address = part[2], .packet = packet, .interval = part[6],
        };
      } else if (type == USB_ENDPOINT_BULK && (part[2] & USB_ENDPOINT_IN) &&
          !candidate.acl_in.address) {
        candidate.acl_in = (struct usb_bulk_endpoint){.address = part[2], .packet = packet};
      } else if (type == USB_ENDPOINT_BULK && !(part[2] & USB_ENDPOINT_IN) &&
          !candidate.acl_out.address) {
        candidate.acl_out = (struct usb_bulk_endpoint){.address = part[2], .packet = packet};
      } else {
        valid = false;
      }
    }
  }
  if (valid && endpoints == AX200_ENDPOINTS && candidate.event.address &&
      candidate.acl_in.address && candidate.acl_out.address) {
    candidate.selected = true;
    *binding = candidate;
  }
}

enum usb_result usb_bluetooth_bind(struct usb_bluetooth_binding *binding,
    struct usb_host_controller *controller, struct usb_host_device *device,
    uint64_t deadline)
{
  if (!binding->selected || usb_host_device_depth(device) ||
      usb_host_device_speed(device) != USB_SPEED_FULL ||
      usb_host_interrupt_capacity() < HCI_EVENT_BYTES ||
      usb_host_async_bulk_capacity() < BLUETOOTH_HCI_ACL_MAX) {
    return USB_UNSUPPORTED;
  }
  struct usb_host_interrupt *event;
  enum usb_result result = usb_host_configure_interrupt_in(device,
      &binding->event, HCI_EVENT_BYTES, USB_INTERRUPT_HCI, deadline, &event);
  if (result == USB_OK) {
    result = usb_host_configure_async_bulk(device, &binding->acl_in,
        &binding->acl_out, BLUETOOTH_HCI_ACL_MAX, deadline);
  }
  if (result != USB_OK) {
    return result;
  }
  struct usb_setup setup = {
    .request_type = USB_REQUEST_DEVICE_OUT,
    .request = USB_REQUEST_SET_CONFIGURATION,
    .value = binding->configuration,
  };
  struct usb_ticket ticket;
  result = usb_host_control_submit(device, &setup, NULL, deadline, &ticket);
  if (result != USB_OK) {
    return result;
  }
  result = usb_host_control_wait(device, ticket, deadline);
  if (result != USB_OK) {
    usb_host_control_abandon(device, ticket);
    return result;
  }
  struct usb_completion completion;
  result = usb_host_control_take(device, ticket, NULL, 0, &completion);
  if (result != USB_OK) {
    usb_host_control_abandon(device, ticket);
    return result;
  }
  if (completion.result != USB_OK || completion.bytes) {
    return completion.result == USB_OK ? USB_IO : completion.result;
  }
  result = usb_host_interrupt_start(event);
  if (result == USB_OK) {
    result = usb_host_async_bulk_start(device);
  }
  if (result == USB_OK) {
    bluetooth_hci_attach(controller, device, AX200_INTERFACE, event);
  }
  return result;
}
