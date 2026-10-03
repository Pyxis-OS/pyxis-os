#ifndef USB_HOST_H
#define USB_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum usb_speed { USB_SPEED_UNKNOWN, USB_SPEED_LOW, USB_SPEED_FULL, USB_SPEED_HIGH, USB_SPEED_SUPER };
enum usb_result { USB_OK, USB_BUSY, USB_INVALID, USB_IO, USB_TIMEOUT, USB_UNSUPPORTED, USB_STALE };

struct usb_host_controller;
struct usb_host_device;
struct usb_setup {
  uint8_t request_type, request;
  uint16_t value, index, length;
};
struct usb_ticket { uint64_t generation; };
struct usb_completion { enum usb_result result; size_t bytes; };
struct usb_bulk_endpoint {
  uint8_t address, max_burst;
  uint16_t max_packet;
};

/* Private BSP controller-worker interfaces. Records and buffers are prepared
 * before AP startup and retained at runtime. Indices cover advertised root ports;
 * an empty index is not evidence that discovery was complete. */
unsigned usb_host_port_count(const struct usb_host_controller *controller);
bool usb_host_inventory_complete(const struct usb_host_controller *controller);
struct usb_host_device *usb_host_device_at(struct usb_host_controller *controller, unsigned index);
bool usb_host_port_present(const struct usb_host_controller *controller, unsigned index);
enum usb_speed usb_host_device_speed(const struct usb_host_device *device);
size_t usb_host_control_capacity(void);
enum usb_result usb_host_address(struct usb_host_device *device, uint64_t deadline);
enum usb_result usb_host_update_packet(struct usb_host_device *device, uint16_t packet, uint64_t deadline);

/* Submission captures setup/outbound data, never a read destination. One client
 * owns the ticket until take/abandon. Wait timeout does not consume or cancel it.
 * Take copies actual bytes only on success; rejected collection keeps the ticket.
 * Abandoned active work and unresolved DMA spans cannot be recycled. */
enum usb_result usb_host_control_submit(struct usb_host_device *device, const struct usb_setup *setup,
                                        const void *outbound, uint64_t deadline, struct usb_ticket *ticket);
enum usb_result usb_host_control_wait(struct usb_host_device *device, struct usb_ticket ticket,
                                      uint64_t deadline);
enum usb_result usb_host_control_take(struct usb_host_device *device, struct usb_ticket ticket,
                                      void *destination, size_t capacity, struct usb_completion *completion);
void usb_host_control_abandon(struct usb_host_device *device, struct usb_ticket ticket);

/* Caller supplies checked descriptor fields. The host owns contexts/rings, not
 * configuration or class selection. The initial non-control ring budget is two. */
enum usb_result usb_host_configure_bulk(struct usb_host_device *device, uint8_t configuration,
                                       uint8_t interface, uint8_t alternate,
                                       const struct usb_bulk_endpoint *endpoints, unsigned count,
                                       uint64_t deadline);

#endif
