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
struct usb_host_interrupt;
enum usb_interrupt_kind { USB_INTERRUPT_HCI, USB_INTERRUPT_HID, USB_INTERRUPT_HUB };
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
  uint8_t address, interval, transactions;
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
/* Outer worker only. Poll leaves active work posted; an owned STALL performs
 * the existing bounded retirement fence before collection. */
enum usb_result usb_host_control_poll(struct usb_host_device *device, struct usb_ticket ticket,
                                      uint64_t deadline);
enum usb_result usb_host_control_wait(struct usb_host_device *device, struct usb_ticket ticket,
                                      uint64_t deadline);
enum usb_result usb_host_control_take(struct usb_host_device *device, struct usb_ticket ticket,
                                      void *destination, size_t capacity, struct usb_completion *completion);
void usb_host_control_abandon(struct usb_host_device *device, struct usb_ticket ticket);

/* BSP/IF=0. Notify the owning controller worker without allocating. */
void usb_host_notify(struct usb_host_controller *controller);

/* Separate prepared HCI, HID and hub pools. Each handle names one IN endpoint;
 * slot, DCI and physical TD identity retain hardware ownership. Configure before
 * SET_CONFIGURATION, start afterwards. HID supports boot USB 2 profiles and
 * claimed runtime LS/FS leaves; hubs are boot-present USB 2 only.
 * Wait expiry leaves receives posted. Take borrows no destination beyond return.
 * Terminal failure precedes queued data; ack_loss follows class source release.
 * Prepared and retired backing is retained until reboot. */
/* HCI and hub receive capacity remains independent from the HID profile. */
size_t usb_host_interrupt_capacity(void);
size_t usb_host_hid_interrupt_capacity(void);
enum usb_result usb_host_configure_interrupt_in(struct usb_host_device *device,
                                                const struct usb_interrupt_endpoint *endpoint,
                                                size_t receive_bytes, enum usb_interrupt_kind kind,
                                                uint64_t deadline, struct usb_host_interrupt **stream);
enum usb_result usb_host_interrupt_start(struct usb_host_interrupt *stream);
enum usb_result usb_host_interrupt_wait(struct usb_host_interrupt *stream, uint64_t deadline);
enum usb_result usb_host_interrupt_take(struct usb_host_interrupt *stream, void *destination,
                                        size_t capacity, struct usb_interrupt_completion *completion);
void usb_host_interrupt_ack_loss(struct usb_host_interrupt *stream);

/* One controller budget covers boot claims and runtime attempts, including
 * failed/retired generations. Runtime attempts use fresh prepared EP0/context
 * slices, never recycle old DMA, and only admit LS/FS leaves. Hub topology is
 * fixed at boot. Port generations advance on consumed connection changes. */
enum usb_result usb_host_hid_claim(struct usb_host_device *device);
enum usb_result usb_host_hid_attach_root(struct usb_host_controller *controller,
                                        unsigned port, uint64_t deadline, struct usb_host_device **device);
enum usb_result usb_host_hid_attach_child(struct usb_host_device *parent, unsigned port,
                                         const struct usb_link *link, uint64_t deadline,
                                         struct usb_host_device **device);
enum usb_result usb_host_hid_retire(struct usb_host_device *device, uint64_t deadline);
bool usb_host_device_present(const struct usb_host_device *device);
bool usb_host_hid_root_present(const struct usb_host_controller *controller, unsigned port);
uint64_t usb_host_port_generation(const struct usb_host_controller *controller, unsigned port);
/* BSP/IF=0. Conservative freshness observation, no USB operation. */
bool usb_host_hid_input_complete(const struct usb_host_controller *controller);

/* Private root/full-speed bulk stream, prepared separately from storage. Host
 * configuration precedes class configuration; start follows it. Receive/take
 * ownership and terminal failures match interrupt IN. Idle receives never time
 * out. OUT captures bytes and owns an independent ticket until take; no wait or
 * cancellation is supplied. Active OUT expiry quarantines uncertain DMA. STALL
 * retains the OUT span and prevents later submission, without recovery. The
 * AX200's one expected zero-byte bootloader IN transaction error is retired by
 * the outer worker's reset/dequeue fence; IN take stays BUSY until real BOOT
 * confirmation permits fresh operational receives. Other errors are terminal. */
size_t usb_host_async_bulk_capacity(void);
enum usb_result usb_host_configure_async_bulk(struct usb_host_device *device,
                                             const struct usb_bulk_endpoint *in,
                                             const struct usb_bulk_endpoint *out,
                                             size_t receive_bytes, uint64_t deadline);
enum usb_result usb_host_async_bulk_start(struct usb_host_device *device);
enum usb_result usb_host_async_bulk_take(struct usb_host_device *device, void *destination,
                                        size_t capacity, struct usb_interrupt_completion *completion);
/* BSP/IF=0, no USB operation. A firmware boot boundary cannot retire until the
 * expected bootloader IN halt is fenced and fresh operational receives posted. */
bool usb_host_async_bulk_in_ready(const struct usb_host_device *device);
enum usb_result usb_host_async_bulk_out_submit(struct usb_host_device *device, const void *bytes,
                                              size_t length, uint64_t deadline, struct usb_ticket *ticket);
enum usb_result usb_host_async_bulk_out_take(struct usb_host_device *device, struct usb_ticket ticket,
                                            struct usb_completion *completion);

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
