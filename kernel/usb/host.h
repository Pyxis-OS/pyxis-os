#ifndef USB_HOST_H
#define USB_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum usb_speed {
  USB_SPEED_UNKNOWN, USB_SPEED_LOW, USB_SPEED_FULL, USB_SPEED_HIGH,
  USB_SPEED_SUPER, USB_SPEED_SUPER_PLUS
};

static inline bool usb_speed_is_enhanced(enum usb_speed speed)
{
  return speed == USB_SPEED_SUPER || speed == USB_SPEED_SUPER_PLUS;
}

/* Directional bit rates include all negotiated lanes, never Rx plus Tx. */
struct usb_link {
  uint64_t rx_bps, tx_bps;
  enum usb_speed speed;
  uint8_t rx_lanes, tx_lanes;
};
#define USB_SUPER_LANE_BPS 5000000000ULL
#define USB_GEN2_LANE_BPS 10000000000ULL

enum usb_result {
  USB_OK, USB_BUSY, USB_INVALID, USB_IO, USB_TIMEOUT, USB_UNSUPPORTED, USB_STALE,
  USB_STALL, USB_DISCONTINUITY
};

struct usb_host_controller;
struct usb_host_device;
struct usb_setup {
  uint8_t request_type, request;
  uint16_t value, index, length;
};
struct usb_ticket { uint64_t generation; };
struct usb_completion { enum usb_result result; size_t bytes; };
struct usb_bulk_endpoint {
  uint8_t address, burst;
  uint16_t packet;
};
struct usb_interrupt_endpoint {
  uint8_t address, interval;
  uint16_t packet;
};
struct usb_interrupt_completion {
  uint64_t sequence;
  size_t bytes;
};

/* Private BSP controller-worker interfaces. Records and buffers are prepared
 * before AP startup and retained at runtime. Indices cover advertised root ports;
 * an empty index is not evidence that discovery was complete. */
unsigned usb_host_descendant_capacity(const struct usb_host_controller *controller);
unsigned usb_host_port_count(const struct usb_host_controller *controller);
bool usb_host_inventory_complete(const struct usb_host_controller *controller);
struct usb_host_device *usb_host_device_at(struct usb_host_controller *controller, unsigned index);
bool usb_host_port_present(const struct usb_host_controller *controller, unsigned index);
unsigned usb_host_device_depth(const struct usb_host_device *device);
enum usb_speed usb_host_device_speed(const struct usb_host_device *device);
size_t usb_host_control_capacity(void);
enum usb_result usb_host_address(struct usb_host_device *device, uint64_t deadline);

/* Boot-only hub metadata and retained child reservation. Attach returns an
 * unaddressed device; the ordinary address/inspection path configures EP0.
 * Ports are one-based. TT think time is the descriptor's encoded value (0-3).
 * Multi-TT requires the core to have selected that interface beforehand.
 * Enhanced child links require a unique controller profile. Link storage is
 * copied before command submission; unknown matches stay unsupported. */
enum usb_result usb_host_configure_hub(struct usb_host_device *device, unsigned ports,
                                       unsigned tt_think_time, bool multi_tt, uint64_t deadline);
enum usb_result usb_host_attach_child(struct usb_host_device *parent, unsigned port,
                                      const struct usb_link *link, uint64_t deadline,
                                      struct usb_host_device **child);
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

/* BSP/IF=0. Notify the owning controller worker without allocating. */
void usb_host_notify(struct usb_host_controller *controller);

/* One retained root/full-speed stream per device, owned by its BSP worker.
 * Configure host contexts before the class selects its USB configuration; start
 * afterwards. Receive length is bounded by the prepared capacity, independently
 * of the endpoint's packet size. No caller destination survives a call.
 * Wait expiry leaves receives posted. Rejected take preserves the queue head.
 * Terminal stream failure takes precedence over queued data and stops rearm;
 * posted/stalled DMA remains retained. There is no cancellation or recovery. */
size_t usb_host_interrupt_capacity(void);
enum usb_result usb_host_configure_interrupt_in(struct usb_host_device *device,
                                                const struct usb_interrupt_endpoint *endpoint,
                                                size_t receive_bytes, uint64_t deadline);
enum usb_result usb_host_interrupt_start(struct usb_host_device *device);
enum usb_result usb_host_interrupt_wait(struct usb_host_device *device, uint64_t deadline);
enum usb_result usb_host_interrupt_take(struct usb_host_device *device, void *destination,
                                        size_t capacity, struct usb_interrupt_completion *completion);

/* Class binding during boot, with retained runtime I/O. A bounded pool reserves
 * two bulk rings and one captured transfer buffer per admitted device before AP
 * startup. Transfers are serialized on the owning worker; failure never copies
 * a read destination. A stall retains its span until bulk_clear retires it. */
enum usb_result usb_host_configure_bulk(struct usb_host_device *device,
                                        const struct usb_bulk_endpoint *in,
                                        const struct usb_bulk_endpoint *out, uint64_t deadline);
/* submitted is optional, borrowed only until return, and sticky: set with IF=0
 * at Normal TRB publication, before any wait or doorbell. */
enum usb_result usb_host_bulk_transfer(struct usb_host_device *device, uint8_t endpoint,
                                       const void *outbound, void *destination, size_t length,
                                       uint64_t deadline, size_t *actual, bool *submitted);
/* Report stalled IN/OUT bytes after successful retirement. Optionally copy IN
 * bytes before another transfer can reuse its buffer. Recovery failure leaves
 * the caller destination untouched. */
enum usb_result usb_host_bulk_clear(struct usb_host_device *device, uint8_t endpoint,
                                   void *destination, size_t capacity, size_t *actual, uint64_t deadline);

#endif
