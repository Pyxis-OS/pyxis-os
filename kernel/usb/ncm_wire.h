#ifndef USB_NCM_WIRE_H
#define USB_NCM_WIRE_H

#include "host.h"
#include "settings.h"

#define USB_NCM_NTB_BYTES USB_NETWORK_BULK_BYTES
#define USB_NCM_FRAME_BYTES 1514u
#define USB_NCM_TX_FRAMES 8u
#define USB_NCM_RX_FRAMES 64u
#define USB_NCM_RX_NDPS 8u
#define USB_NCM_PARAMETERS_BYTES 28u

#define USB_NCM_CAP_PACKET_FILTER (1u << 0)
#define USB_NCM_CAP_NET_ADDRESS (1u << 1)
#define USB_NCM_CAP_ENCAPSULATED (1u << 2)
#define USB_NCM_CAP_MAX_DATAGRAM (1u << 3)
#define USB_NCM_CAP_CRC_MODE (1u << 4)
#define USB_NCM_CAP_INPUT_DATAGRAMS (1u << 5)
#define USB_NCM_FORMAT_NTB16 (1u << 0)
#define USB_NCM_FORMAT_NTB32 (1u << 1)

enum usb_ncm_wire_result {
  USB_NCM_WIRE_OK, USB_NCM_WIRE_INVALID, USB_NCM_WIRE_UNSUPPORTED,
  USB_NCM_WIRE_LIMIT, USB_NCM_WIRE_MORE
};

struct usb_ncm_function {
  uint8_t configuration, control_interface, data_interface, data_alternate;
  uint8_t mac_string, capabilities;
  uint16_t max_datagram;
  struct usb_interrupt_endpoint notification;
  struct usb_bulk_endpoint in, out;
  bool selected, invalid, ambiguous;
};

/* Start with a zeroed function and accumulate every checked configuration.
 * Selected facts are copied; no descriptor pointer survives this call. */
void usb_ncm_wire_select(struct usb_ncm_function *function,
    const uint8_t *configuration, size_t bytes, enum usb_speed speed);

struct usb_ncm_parameters {
  uint32_t rx_bytes, tx_bytes, tx_max_bytes;
  uint16_t rx_divisor, rx_remainder, rx_alignment;
  uint16_t tx_divisor, tx_remainder, tx_alignment;
  uint16_t formats, max_tx_datagrams;
};

/* Sizes are bounded preparation choices; tx_max_bytes retains the actual
 * device maximum needed to decide whether a packet-aligned OUT needs a ZLP. */
enum usb_ncm_wire_result usb_ncm_parameters_parse(struct usb_ncm_parameters *parameters,
    const uint8_t *data, size_t bytes);
bool usb_ncm_mac_parse(const uint8_t *descriptor, size_t bytes, uint8_t mac[6]);

struct usb_ncm_frame {
  const uint8_t *data;
  size_t bytes;
};
struct usb_ncm_span {
  uint16_t offset, bytes;
};
struct usb_ncm_ntb {
  struct usb_ncm_span frames[USB_NCM_RX_FRAMES];
  uint16_t sequence;
  unsigned count;
};

/* Encode a whole-frame prefix, at most eight. Destination must not overlap
 * frame storage; packet is the checked high/SuperSpeed bulk packet size.
 * On success frames_used names exactly the frames owned by this NTB. Padding
 * avoids a ZLP except at the actual device OUT maximum, where it is forbidden. */
enum usb_ncm_wire_result usb_ncm_ntb16_encode(const struct usb_ncm_parameters *parameters,
    uint16_t packet, uint16_t sequence, const struct usb_ncm_frame *frames, unsigned count,
    uint8_t *destination, size_t capacity, unsigned *frames_used, size_t *bytes);
/* Validate the complete USB receive before returning any borrowed frame spans.
 * Empty NDPs are valid. Sequence is observed, not treated as delivery authority.
 * Output remains untouched on failure. Caller retains the input through use. */
enum usb_ncm_wire_result usb_ncm_ntb16_validate(const struct usb_ncm_parameters *parameters,
    const uint8_t *data, size_t bytes, struct usb_ncm_ntb *ntb);

enum usb_ncm_notification_kind {
  USB_NCM_CONNECTION, USB_NCM_SPEED, USB_NCM_RESPONSE_AVAILABLE
};
struct usb_ncm_notification {
  enum usb_ncm_notification_kind kind;
  bool connected;
  uint32_t downstream_bps, upstream_bps;
};
/* Decode one prefix; MORE means the collector must retain its fragment.
 * A speed notice is sixteen bytes, commonly received as an 8+8 split.
 * consumed is zero on failure; output changes only after complete validation. */
enum usb_ncm_wire_result usb_ncm_notification_decode(const uint8_t *data, size_t bytes,
    uint8_t control_interface, struct usb_ncm_notification *notification, size_t *consumed);

#endif
