#include <kernel/memory.h>
#include "ncm_wire.h"

#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_STRING 3
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT 5
#define USB_DESCRIPTOR_CLASS_INTERFACE 0x24
#define USB_DESCRIPTOR_SUPER_COMPANION 0x30
#define USB_CLASS_COMMUNICATIONS 2
#define USB_CLASS_DATA 0x0a
#define USB_SUBCLASS_NCM 0x0d
#define USB_PROTOCOL_NCM_DATA 1
#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_NUMBER 0x0f
#define USB_ENDPOINT_RESERVED 0x70
#define USB_ENDPOINT_BULK 2
#define USB_ENDPOINT_INTERRUPT 3
#define USB_PACKET_SIZE_MASK 0x07ff
#define USB_PACKET_TRANSACTION_MASK 0x1800
#define USB_PACKET_RESERVED_MASK 0xe000
#define USB_PACKET_TRANSACTION_SHIFT 11
#define USB_HIGH_BULK_PACKET 512u
#define USB_SUPER_BULK_PACKET 1024u
#define USB_CDC_VERSION_1_10 0x0110
#define USB_CDC_VERSION_1_20 0x0120
#define USB_NCM_VERSION_1_00 0x0100
#define USB_CDC_HEADER 0
#define USB_CDC_UNION 6
#define USB_CDC_ETHERNET 0x0f
#define USB_CDC_NCM 0x1a
#define USB_CDC_CAPABILITIES_MASK 0x3f
#define USB_NOTIFICATION_REQUEST_TYPE 0xa1
#define USB_NOTIFICATION_CONNECTION 0
#define USB_NOTIFICATION_RESPONSE 1
#define USB_NOTIFICATION_SPEED 0x2a
#define NCM_NTH16_SIGNATURE 0x484d434e
#define NCM_NDP16_SIGNATURE 0x304d434e
#define NCM_NDP16_CRC_SIGNATURE 0x314d434e
#define NCM_NTH16_BYTES 12u
#define NCM_NDP16_HEADER_BYTES 8u
#define NCM_NDP16_MIN_BYTES 16u
#define NCM_POINTER_BYTES 4u
#define NCM_MIN_NTB_BYTES 2048u
#define ETHERNET_HEADER_BYTES 14u

static uint16_t read16(const uint8_t *data)
{
  return data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read32(const uint8_t *data)
{
  return read16(data) | ((uint32_t)read16(data + 2) << 16);
}

static void write16(uint8_t *data, uint16_t value)
{
  data[0] = value;
  data[1] = value >> 8;
}

static void write32(uint8_t *data, uint32_t value)
{
  write16(data, value);
  write16(data + 2, value >> 16);
}

static bool descriptor_at(const uint8_t *data, size_t bytes, size_t offset)
{
  return offset <= bytes && bytes - offset >= 2 && data[offset] >= 2 &&
    data[offset] <= bytes - offset;
}

static bool interrupt_endpoint(const uint8_t *data, size_t bytes, size_t offset,
    enum usb_speed speed, struct usb_interrupt_endpoint *endpoint)
{
  const uint8_t *part = data + offset;
  if (part[0] < 7 || !(part[2] & USB_ENDPOINT_IN) || !(part[2] & USB_ENDPOINT_NUMBER) ||
      (part[2] & USB_ENDPOINT_RESERVED) || part[3] != USB_ENDPOINT_INTERRUPT ||
      !part[6] || part[6] > 16) {
    return false;
  }
  uint16_t wire_packet = read16(part + 4);
  *endpoint = (struct usb_interrupt_endpoint){
    .address = part[2], .packet = wire_packet & USB_PACKET_SIZE_MASK, .interval = part[6],
  };
  if (!endpoint->packet || endpoint->packet > 1024 || (wire_packet & USB_PACKET_RESERVED_MASK)) {
    return false;
  }
  if (speed == USB_SPEED_HIGH) {
    endpoint->transactions = (wire_packet >> USB_PACKET_TRANSACTION_SHIFT) & 3;
    return endpoint->transactions != 3;
  }
  if (!usb_speed_is_enhanced(speed) || (wire_packet & USB_PACKET_TRANSACTION_MASK)) {
    return false;
  }
  offset += part[0];
  if (!descriptor_at(data, bytes, offset) || data[offset] != 6 ||
      data[offset + 1] != USB_DESCRIPTOR_SUPER_COMPANION || data[offset + 2] > 15 ||
      data[offset + 3]) {
    return false;
  }
  endpoint->burst = data[offset + 2];
  endpoint->bytes_per_interval = read16(data + offset + 4);
  return endpoint->bytes_per_interval &&
    endpoint->bytes_per_interval <= endpoint->packet * (endpoint->burst + 1);
}

static bool bulk_endpoint(const uint8_t *data, size_t bytes, size_t offset,
    enum usb_speed speed, struct usb_bulk_endpoint *endpoint)
{
  const uint8_t *part = data + offset;
  if (part[0] < 7 || !(part[2] & USB_ENDPOINT_NUMBER) ||
      (part[2] & USB_ENDPOINT_RESERVED) || part[3] != USB_ENDPOINT_BULK) {
    return false;
  }
  *endpoint = (struct usb_bulk_endpoint){.address = part[2], .packet = read16(part + 4)};
  if (speed == USB_SPEED_HIGH) {
    return endpoint->packet == USB_HIGH_BULK_PACKET;
  }
  if (!usb_speed_is_enhanced(speed) || endpoint->packet != USB_SUPER_BULK_PACKET) {
    return false;
  }
  offset += part[0];
  if (!descriptor_at(data, bytes, offset) || data[offset] != 6 ||
      data[offset + 1] != USB_DESCRIPTOR_SUPER_COMPANION || data[offset + 2] > 15 ||
      data[offset + 3] || read16(data + offset + 4)) {
    return false;
  }
  endpoint->burst = data[offset + 2];
  return true;
}

static bool select_data(struct usb_ncm_function *function, const uint8_t *data,
    size_t bytes, enum usb_speed speed)
{
  bool alternate_zero = false, alternate_one = false;
  unsigned endpoints = 0;
  uint8_t alternate = 0;
  bool matching = false;
  for (size_t offset = data[0]; offset < bytes; offset += data[offset]) {
    const uint8_t *part = data + offset;
    if (part[1] == USB_DESCRIPTOR_INTERFACE) {
      if (matching && endpoints != (alternate ? 2u : 0u)) {
        return false;
      }
      matching = part[2] == function->data_interface;
      endpoints = 0;
      if (!matching) {
        continue;
      }
      alternate = part[3];
      if (part[5] != USB_CLASS_DATA || part[6] || part[7] != USB_PROTOCOL_NCM_DATA ||
          alternate > 1 || part[4] != (alternate ? 2 : 0)) {
        return false;
      }
      if (alternate ? alternate_one : alternate_zero) {
        return false;
      }
      if (alternate) {
        alternate_one = true;
      } else {
        alternate_zero = true;
      }
    } else if (matching && part[1] == USB_DESCRIPTOR_ENDPOINT) {
      struct usb_bulk_endpoint endpoint;
      if (!alternate || ++endpoints > 2 || !bulk_endpoint(data, bytes, offset, speed, &endpoint)) {
        return false;
      }
      struct usb_bulk_endpoint *target = endpoint.address & USB_ENDPOINT_IN ?
        &function->in : &function->out;
      if (target->address) {
        return false;
      }
      *target = endpoint;
    }
  }
  return alternate_zero && alternate_one && (!matching || endpoints == (alternate ? 2u : 0u)) &&
    function->in.address && function->out.address &&
    function->notification.address != function->in.address;
}

static bool select_function(struct usb_ncm_function *function, const uint8_t *data,
    size_t bytes, size_t control, enum usb_speed speed)
{
  const uint8_t *interface = data + control;
  if (interface[2] >= data[4] || interface[3] || interface[4] != 1 || interface[7] ||
      (speed != USB_SPEED_HIGH && !usb_speed_is_enhanced(speed))) {
    return false;
  }
  *function = (struct usb_ncm_function){
    .configuration = data[5], .control_interface = interface[2], .data_alternate = 1,
  };
  bool header = false, union_seen = false, ethernet = false, ncm = false;
  bool endpoint_seen = false;
  for (size_t offset = control + interface[0]; offset < bytes; offset += data[offset]) {
    const uint8_t *part = data + offset;
    if (part[1] == USB_DESCRIPTOR_INTERFACE) {
      break;
    }
    if (part[1] == USB_DESCRIPTOR_CLASS_INTERFACE) {
      if (part[0] < 3 || endpoint_seen || (!header && part[2] != USB_CDC_HEADER)) {
        return false;
      }
      switch (part[2]) {
      case USB_CDC_HEADER:
        if (header || part[0] != 5 ||
            (read16(part + 3) != USB_CDC_VERSION_1_10 && read16(part + 3) != USB_CDC_VERSION_1_20)) {
          return false;
        }
        header = true;
        break;
      case USB_CDC_UNION:
        if (union_seen || part[0] != 5 || part[3] != function->control_interface ||
            part[4] == function->control_interface || part[4] >= data[4]) {
          return false;
        }
        function->data_interface = part[4];
        union_seen = true;
        break;
      case USB_CDC_ETHERNET:
        if (ethernet || part[0] != 13 || !part[3] || read16(part + 8) < USB_NCM_FRAME_BYTES) {
          return false;
        }
        function->mac_string = part[3];
        function->max_datagram = read16(part + 8);
        ethernet = true;
        break;
      case USB_CDC_NCM:
        if (ncm || part[0] != 6 || read16(part + 3) != USB_NCM_VERSION_1_00) {
          return false;
        }
        function->capabilities = part[5] & USB_CDC_CAPABILITIES_MASK;
        ncm = true;
        break;
      default:
        break;
      }
    } else if (part[1] == USB_DESCRIPTOR_ENDPOINT) {
      if (endpoint_seen || !header || !union_seen || !ethernet || !ncm ||
          !interrupt_endpoint(data, bytes, offset, speed, &function->notification)) {
        return false;
      }
      endpoint_seen = true;
    }
  }
  return header && union_seen && ethernet && ncm && endpoint_seen &&
    select_data(function, data, bytes, speed);
}

void usb_ncm_wire_select(struct usb_ncm_function *function,
    const uint8_t *data, size_t bytes, enum usb_speed speed)
{
  if (!function) {
    return;
  }
  if (!data || bytes < 9 || data[0] != 9 || data[1] != USB_DESCRIPTOR_CONFIGURATION ||
      read16(data + 2) != bytes || !data[4] || !data[5]) {
    function->invalid = true;
    return;
  }
  /* Validate traversal before any class scan reads fixed descriptor fields. */
  for (size_t offset = data[0]; offset < bytes; offset += data[offset]) {
    if (!descriptor_at(data, bytes, offset) ||
        (data[offset + 1] == USB_DESCRIPTOR_INTERFACE && data[offset] != 9) ||
        (data[offset + 1] == USB_DESCRIPTOR_ENDPOINT && data[offset] < 7)) {
      function->invalid = true;
      return;
    }
  }
  for (size_t offset = data[0]; offset < bytes; offset += data[offset]) {
    const uint8_t *part = data + offset;
    if (part[1] != USB_DESCRIPTOR_INTERFACE || part[5] != USB_CLASS_COMMUNICATIONS ||
        part[6] != USB_SUBCLASS_NCM) {
      continue;
    }
    struct usb_ncm_function candidate;
    if (!select_function(&candidate, data, bytes, offset, speed)) {
      function->invalid = true;
    } else if (function->selected) {
      function->ambiguous = true;
    } else {
      bool invalid = function->invalid, ambiguous = function->ambiguous;
      *function = candidate;
      function->selected = true;
      function->invalid = invalid;
      function->ambiguous = ambiguous;
    }
  }
}

static bool alignment_valid(uint16_t divisor, uint16_t remainder, uint16_t alignment)
{
  return divisor >= 4 && !(divisor & (divisor - 1)) && remainder < divisor &&
    alignment >= 4 && !(alignment & (alignment - 1));
}

enum usb_ncm_wire_result usb_ncm_parameters_parse(struct usb_ncm_parameters *parameters,
    const uint8_t *data, size_t bytes)
{
  if (!parameters || !data || bytes != USB_NCM_PARAMETERS_BYTES ||
      read16(data) != USB_NCM_PARAMETERS_BYTES) {
    return USB_NCM_WIRE_INVALID;
  }
  struct usb_ncm_parameters result = {
    .formats = read16(data + 2) & (USB_NCM_FORMAT_NTB16 | USB_NCM_FORMAT_NTB32),
    .rx_bytes = read32(data + 4), .rx_divisor = read16(data + 8),
    .rx_remainder = read16(data + 10), .rx_alignment = read16(data + 12),
    .tx_bytes = read32(data + 16), .tx_max_bytes = read32(data + 16),
    .tx_divisor = read16(data + 20), .tx_remainder = read16(data + 22),
    .tx_alignment = read16(data + 24), .max_tx_datagrams = read16(data + 26),
  };
  if (!alignment_valid(result.rx_divisor, result.rx_remainder, result.rx_alignment) ||
      !alignment_valid(result.tx_divisor, result.tx_remainder, result.tx_alignment)) {
    return USB_NCM_WIRE_INVALID;
  }
  if (!(result.formats & USB_NCM_FORMAT_NTB16) || result.rx_bytes < NCM_MIN_NTB_BYTES ||
      result.tx_bytes < NCM_NTH16_BYTES + NCM_NDP16_MIN_BYTES + ETHERNET_HEADER_BYTES) {
    return USB_NCM_WIRE_UNSUPPORTED;
  }
  if (result.rx_bytes > USB_NCM_NTB_BYTES) {
    result.rx_bytes = USB_NCM_NTB_BYTES;
  }
  if (result.tx_bytes > USB_NCM_NTB_BYTES) {
    result.tx_bytes = USB_NCM_NTB_BYTES;
  }
  if (result.rx_alignment > result.rx_bytes - NCM_NDP16_MIN_BYTES ||
      result.tx_alignment > result.tx_bytes - NCM_NDP16_MIN_BYTES) {
    return USB_NCM_WIRE_UNSUPPORTED;
  }
  *parameters = result;
  return USB_NCM_WIRE_OK;
}

static int hex_digit(uint8_t value)
{
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return -1;
}

bool usb_ncm_mac_parse(const uint8_t *data, size_t bytes, uint8_t mac[6])
{
  if (!data || !mac || bytes != 26 || data[0] != bytes || data[1] != USB_DESCRIPTOR_STRING) {
    return false;
  }
  uint8_t result[6], nonzero = 0;
  for (unsigned i = 0; i < 6; ++i) {
    size_t offset = 2 + i * 4;
    int high = hex_digit(data[offset]), low = hex_digit(data[offset + 2]);
    if (data[offset + 1] || data[offset + 3] || high < 0 || low < 0) {
      return false;
    }
    result[i] = (high << 4) | low;
    nonzero |= result[i];
  }
  if (!nonzero || (result[0] & 1)) {
    return false;
  }
  memcpy(mac, result, sizeof(result));
  return true;
}

static size_t align_payload(size_t offset, uint16_t divisor, uint16_t remainder)
{
  size_t residue = (offset + ETHERNET_HEADER_BYTES) % divisor;
  return offset + (remainder + divisor - residue) % divisor;
}

enum usb_ncm_wire_result usb_ncm_ntb16_encode(const struct usb_ncm_parameters *parameters,
    uint16_t packet, uint16_t sequence, const struct usb_ncm_frame *frames, unsigned count,
    uint8_t *destination, size_t capacity, unsigned *frames_used, size_t *bytes)
{
  if (!parameters || !frames || !destination || !frames_used || !bytes ||
      !count || count > USB_NCM_TX_FRAMES ||
      (packet != USB_HIGH_BULK_PACKET && packet != USB_SUPER_BULK_PACKET) ||
      !alignment_valid(parameters->tx_divisor, parameters->tx_remainder, parameters->tx_alignment) ||
      parameters->tx_bytes > USB_NCM_NTB_BYTES || parameters->tx_bytes > parameters->tx_max_bytes) {
    return USB_NCM_WIRE_INVALID;
  }
  *frames_used = 0;
  *bytes = 0;
  if (capacity > parameters->tx_bytes) {
    capacity = parameters->tx_bytes;
  }
  unsigned limit = count;
  if (parameters->max_tx_datagrams && limit > parameters->max_tx_datagrams) {
    limit = parameters->max_tx_datagrams;
  }
  for (unsigned i = 0; i < limit; ++i) {
    if (!frames[i].data || frames[i].bytes < ETHERNET_HEADER_BYTES ||
        frames[i].bytes > USB_NCM_FRAME_BYTES) {
      return USB_NCM_WIRE_INVALID;
    }
  }
  size_t ndp = (NCM_NTH16_BYTES + parameters->tx_alignment - 1) &
    ~(size_t)(parameters->tx_alignment - 1);
  size_t cursor = ndp + NCM_NDP16_HEADER_BYTES + (limit + 1) * NCM_POINTER_BYTES;
  struct usb_ncm_span spans[USB_NCM_TX_FRAMES];
  unsigned used = 0;
  for (unsigned i = 0; i < limit; ++i) {
    size_t offset = align_payload(cursor, parameters->tx_divisor, parameters->tx_remainder);
    if (offset > capacity || frames[i].bytes > capacity - offset) {
      break;
    }
    size_t end = offset + frames[i].bytes;
    if (end % packet == 0 && end != parameters->tx_max_bytes && end == capacity) {
      break;
    }
    spans[used++] = (struct usb_ncm_span){.offset = offset, .bytes = frames[i].bytes};
    cursor = end;
  }
  if (!used) {
    return USB_NCM_WIRE_LIMIT;
  }
  if (cursor % packet == 0 && cursor != parameters->tx_max_bytes) {
    ++cursor;
  }
  memset(destination, 0, cursor);
  write32(destination, NCM_NTH16_SIGNATURE);
  write16(destination + 4, NCM_NTH16_BYTES);
  write16(destination + 6, sequence);
  write16(destination + 8, cursor);
  write16(destination + 10, ndp);
  write32(destination + ndp, NCM_NDP16_SIGNATURE);
  write16(destination + ndp + 4, NCM_NDP16_HEADER_BYTES + (used + 1) * NCM_POINTER_BYTES);
  for (unsigned i = 0; i < used; ++i) {
    write16(destination + ndp + NCM_NDP16_HEADER_BYTES + i * NCM_POINTER_BYTES, spans[i].offset);
    write16(destination + ndp + NCM_NDP16_HEADER_BYTES + i * NCM_POINTER_BYTES + 2, spans[i].bytes);
    memcpy(destination + spans[i].offset, frames[i].data, spans[i].bytes);
  }
  *frames_used = used;
  *bytes = cursor;
  return USB_NCM_WIRE_OK;
}

static bool overlaps(struct usb_ncm_span first, struct usb_ncm_span second)
{
  return first.offset < (size_t)second.offset + second.bytes &&
    second.offset < (size_t)first.offset + first.bytes;
}

enum usb_ncm_wire_result usb_ncm_ntb16_validate(const struct usb_ncm_parameters *parameters,
    const uint8_t *data, size_t bytes, struct usb_ncm_ntb *ntb)
{
  if (!parameters || !data || !ntb || bytes < NCM_NTH16_BYTES ||
      bytes > parameters->rx_bytes || parameters->rx_bytes > USB_NCM_NTB_BYTES ||
      !alignment_valid(parameters->rx_divisor, parameters->rx_remainder, parameters->rx_alignment) ||
      read32(data) != NCM_NTH16_SIGNATURE || read16(data + 4) != NCM_NTH16_BYTES) {
    return USB_NCM_WIRE_INVALID;
  }
  size_t block = read16(data + 8);
  if (!block) {
    if (bytes == parameters->rx_bytes) {
      return USB_NCM_WIRE_INVALID;
    }
    block = bytes;
  }
  if (block < NCM_NTH16_BYTES || block > bytes) {
    return USB_NCM_WIRE_INVALID;
  }
  struct usb_ncm_ntb result = {.sequence = read16(data + 6)};
  struct usb_ncm_span ndps[USB_NCM_RX_NDPS];
  unsigned ndp_count = 0;
  size_t offset = read16(data + 10);
  if (!offset) {
    return USB_NCM_WIRE_INVALID;
  }
  while (offset) {
    if (offset < NCM_NTH16_BYTES || offset % parameters->rx_alignment ||
        offset > block || block - offset < NCM_NDP16_MIN_BYTES) {
      return USB_NCM_WIRE_INVALID;
    }
    for (unsigned i = 0; i < ndp_count; ++i) {
      if (ndps[i].offset == offset) {
        return USB_NCM_WIRE_INVALID;
      }
    }
    if (ndp_count == USB_NCM_RX_NDPS) {
      return USB_NCM_WIRE_LIMIT;
    }
    uint32_t signature = read32(data + offset);
    if (signature != NCM_NDP16_SIGNATURE) {
      return signature == NCM_NDP16_CRC_SIGNATURE ? USB_NCM_WIRE_UNSUPPORTED : USB_NCM_WIRE_INVALID;
    }
    size_t length = read16(data + offset + 4);
    if (length < NCM_NDP16_MIN_BYTES || length % NCM_POINTER_BYTES || length > block - offset) {
      return USB_NCM_WIRE_INVALID;
    }
    ndps[ndp_count++] = (struct usb_ncm_span){.offset = offset, .bytes = length};
    bool terminated = false;
    for (size_t entry = NCM_NDP16_HEADER_BYTES; entry < length; entry += NCM_POINTER_BYTES) {
      struct usb_ncm_span span = {
        .offset = read16(data + offset + entry), .bytes = read16(data + offset + entry + 2),
      };
      /* Either zero field terminates this NDP, including its ignored suffix. */
      if (!span.offset || !span.bytes) {
        terminated = true;
        break;
      }
      if (span.offset < NCM_NTH16_BYTES || span.bytes < ETHERNET_HEADER_BYTES ||
          span.offset > block || span.bytes > block - span.offset ||
          ((size_t)span.offset + ETHERNET_HEADER_BYTES) % parameters->rx_divisor != parameters->rx_remainder) {
        return USB_NCM_WIRE_INVALID;
      }
      if (span.bytes > USB_NCM_FRAME_BYTES) {
        return USB_NCM_WIRE_UNSUPPORTED;
      }
      if (result.count == USB_NCM_RX_FRAMES) {
        return USB_NCM_WIRE_LIMIT;
      }
      result.frames[result.count++] = span;
    }
    if (!terminated) {
      return USB_NCM_WIRE_INVALID;
    }
    offset = read16(data + offset + 6);
  }
  for (unsigned i = 0; i < ndp_count; ++i) {
    for (unsigned j = 0; j < i; ++j) {
      if (overlaps(ndps[i], ndps[j])) {
        return USB_NCM_WIRE_INVALID;
      }
    }
    for (unsigned j = 0; j < result.count; ++j) {
      if (overlaps(ndps[i], result.frames[j])) {
        return USB_NCM_WIRE_INVALID;
      }
    }
  }
  for (unsigned i = 0; i < result.count; ++i) {
    for (unsigned j = 0; j < i; ++j) {
      if (overlaps(result.frames[i], result.frames[j])) {
        return USB_NCM_WIRE_INVALID;
      }
    }
  }
  *ntb = result;
  return USB_NCM_WIRE_OK;
}

enum usb_ncm_wire_result usb_ncm_notification_decode(const uint8_t *data, size_t bytes,
    uint8_t control_interface, struct usb_ncm_notification *notification, size_t *consumed)
{
  if (!data || !notification || !consumed) {
    return USB_NCM_WIRE_INVALID;
  }
  *consumed = 0;
  if (bytes < 8) {
    return USB_NCM_WIRE_MORE;
  }
  if (data[0] != USB_NOTIFICATION_REQUEST_TYPE || read16(data + 4) != control_interface) {
    return USB_NCM_WIRE_INVALID;
  }
  uint16_t value = read16(data + 2), length = read16(data + 6);
  struct usb_ncm_notification result = {0};
  size_t size = 8;
  switch (data[1]) {
  case USB_NOTIFICATION_CONNECTION:
    if (value > 1 || length) {
      return USB_NCM_WIRE_INVALID;
    }
    result.kind = USB_NCM_CONNECTION;
    result.connected = value != 0;
    break;
  case USB_NOTIFICATION_SPEED:
    if (value || length != 8) {
      return USB_NCM_WIRE_INVALID;
    }
    if (bytes < 16) {
      return USB_NCM_WIRE_MORE;
    }
    result.kind = USB_NCM_SPEED;
    result.downstream_bps = read32(data + 8);
    result.upstream_bps = read32(data + 12);
    size = 16;
    break;
  case USB_NOTIFICATION_RESPONSE:
    if (value || length) {
      return USB_NCM_WIRE_INVALID;
    }
    result.kind = USB_NCM_RESPONSE_AVAILABLE;
    break;
  default:
    return USB_NCM_WIRE_UNSUPPORTED;
  }
  *notification = result;
  *consumed = size;
  return USB_NCM_WIRE_OK;
}
