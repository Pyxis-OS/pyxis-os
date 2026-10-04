#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/pci.h>
#include <kernel/task.h>
#include <limits.h>
#include <stdatomic.h>
#include "core.h"
#include "bot.h"
#include "block.h"
#include "../storage/block_registry.h"
#include "host.h"
#include "settings.h"

#define USB_REQUEST_GET_DESCRIPTOR 6
#define USB_REQUEST_DEVICE_IN 0x80
#define USB_DESCRIPTOR_DEVICE 1
#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT 5
#define USB_DESCRIPTOR_INTERFACE_ASSOCIATION 11
#define USB_DESCRIPTOR_SUPER_COMPANION 48
#define USB_DESCRIPTOR_SSP_ISO_COMPANION 49
#define USB_DEVICE_BYTES 18
#define USB_DEVICE_PREFIX_BYTES 8
#define USB_CONFIGURATION_BYTES 9
#define USB_INTERFACE_BYTES 9
#define USB_ENDPOINT_BYTES 7
#define USB_SUPER_COMPANION_BYTES 6
#define USB_CLASS_HUB 0x09
#define USB_ENDPOINT_IN 0x80
#define USB_REQUEST_GET_STATUS 0
#define USB_REQUEST_CLEAR_FEATURE 1
#define USB_REQUEST_SET_FEATURE 3
#define USB_REQUEST_SET_CONFIGURATION 9
#define USB_REQUEST_DEVICE_OUT 0x00
#define USB_REQUEST_HUB_IN 0xa0
#define USB_REQUEST_PORT_IN 0xa3
#define USB_REQUEST_PORT_OUT 0x23
#define USB_DESCRIPTOR_BOS 15
#define USB_DESCRIPTOR_CAPABILITY 16
#define USB_CAPABILITY_SUPER 3
#define USB_SUPER_CAPABILITY_BYTES 10
#define USB_SUPER_CAPABILITY_SPEED (1u << 3)
#define USB_CAPABILITY_SUPER_PLUS 10
#define USB_BOS_BYTES 5
#define USB_SSP_CAPABILITY_BYTES 12
#define USB_SSP_ATTRIBUTE_COUNT_MASK 0x1f
#define USB_SSP_ID_COUNT_SHIFT 5
#define USB_SSP_ID_COUNT_MASK 0x0f
#define USB_SSP_FUNCTION_RESERVED 0x00f0
#define USB_SSP_ATTRIBUTES_RESERVED 0xfffffe00u
#define USB_SSP_ID_MASK 0x0f
#define USB_SSP_IDS (USB_SSP_ID_MASK + 1)
#define USB_SSP_EXPONENT_SHIFT 4
#define USB_SSP_EXPONENT_MASK 3
#define USB_SSP_ASYMMETRIC (1u << 6)
#define USB_SSP_TRANSMIT (1u << 7)
#define USB_SSP_RESERVED 0x3f00
#define USB_SSP_PROTOCOL_SHIFT 14
#define USB_SSP_PROTOCOL_MASK 3
#define USB_SSP_PROTOCOL_SUPER 0
#define USB_SSP_PROTOCOL_SUPER_PLUS 1
#define USB_SSP_MANTISSA_SHIFT 16
#define USB_DESCRIPTOR_SUPER_HUB 0x2a
#define USB_SUPER_HUB_BYTES 12
#define USB_SUPER_HUB_MAX_PORTS 15
#define USB_SUPER_HUB_MAX_DEPTH 4
#define USB_SUPER_HUB_CHARACTERISTICS_RESERVED 0xffe0
#define USB_HUB_POWER_SWITCH_MASK 3
#define USB_SUPER_HUB_HEADER_LATENCY_MAX 10
#define USB_REQUEST_SET_HUB_DEPTH 12
#define USB_REQUEST_HUB_OUT 0x20
#define USB_SUPER_PORT_POWER 0x0200
#define USB_SUPER_PORT_RESERVED 0xfc04
#define USB_SUPER_CHANGE_RESERVED 0xff06
#define USB_SUPER_LINK_SHIFT 5
#define USB_SUPER_LINK_MASK 0x0f
#define USB_SUPER_LINK_RESERVED_FIRST 12
#define USB_SUPER_LINK_INACTIVE 6
#define USB_SUPER_LINK_COMPLIANCE 10
#define USB_SUPER_LINK_LOOPBACK 11
#define USB_SUPER_CHANGE_CONFIG_ERROR 0x0080
#define USB_SUPER_FEATURE_CHANGE_BH_RESET 29
#define USB_SUPER_FEATURE_CHANGE_LINK 25
#define USB_SUPER_FEATURE_CHANGE_CONFIG_ERROR 26
#define USB_SUPER_EXT_STATUS 2
#define USB_SUPER_EXT_STATUS_BYTES 8
#define USB_SUPER_EXT_TX_ID_SHIFT 4
#define USB_SUPER_EXT_RX_LANES_SHIFT 8
#define USB_SUPER_EXT_TX_LANES_SHIFT 12
#define USB_SUPER_EXT_RESERVED 0xffff0000u
#define USB_DESCRIPTOR_HUB 0x29
#define USB_HUB_PREFIX_BYTES 7
#define USB_HUB_STATUS_BYTES 4
#define USB_SIGNAL_ATTACH_MS 100
#define USB_HUB_OVERCURRENT 0x02
#define USB_HUB_CHARACTERISTICS_RESERVED 0xff00
#define USB_HUB_TT_SHIFT 5
#define USB_HUB_TT_MASK 0x03
#define USB_HUB_PORT_CONNECTION 0x0001
#define USB_HUB_PORT_ENABLE 0x0002
#define USB_HUB_PORT_SUSPEND 0x0004
#define USB_HUB_PORT_OVERCURRENT 0x0008
#define USB_HUB_PORT_RESET 0x0010
#define USB_HUB_PORT_POWER 0x0100
#define USB_HUB_PORT_LOW_SPEED 0x0200
#define USB_HUB_PORT_HIGH_SPEED 0x0400
#define USB_HUB_PORT_RESERVED 0xe0e0
#define USB_HUB_CHANGE_RESERVED 0xffe0
#define USB_HUB_CHANGE_CONNECTION 0x0001
#define USB_HUB_CHANGE_RESET 0x0010
#define USB_HUB_FEATURE_RESET 4
#define USB_HUB_FEATURE_POWER 8
#define USB_HUB_FEATURE_CHANGE_FIRST 16
#define USB_ENDPOINT_TYPE_MASK 0x03
#define USB_ENDPOINT_CONTROL 0x00
#define USB_ENDPOINT_ISOCHRONOUS 0x01
#define USB_ENDPOINT_BULK 0x02
#define USB_ENDPOINT_INTERRUPT 0x03
#define USB_ENDPOINT_NUMBER_MASK 0x0f
#define USB_ENDPOINT_ADDRESS_RESERVED 0x70
#define USB_ENDPOINT_INTERRUPT_RESERVED 0x0c
#define USB_ENDPOINT_USAGE_SHIFT 4
#define USB_ENDPOINT_ISO_RESERVED 0xc0
#define USB_ENDPOINT_ISO_USAGE_MASK 0x30
#define USB_COMPANION_STREAMS_MASK 0x1f
#define USB_COMPANION_BULK_RESERVED 0xe0
#define USB_COMPANION_ISO_MULT_MASK 0x03
#define USB_COMPANION_ISO_RESERVED 0x7c
#define USB_COMPANION_SSP_ISO 0x80
#define USB_PACKET_SIZE_MASK 0x07ff
#define USB_PACKET_RESERVED 0xe000
#define USB_PACKET_TRANSACTIONS 0x1800
#define USB_CONFIGURATION_RESERVED 0x1f
#define USB_CONFIGURATION_REQUIRED 0x80
#define USB_BITSET_BYTES 32

#define PCI_CLASS_SERIAL_BUS 0x0c
#define PCI_SUBCLASS_USB 0x03
#define PCI_INTERFACE_XHCI 0x30

struct usb_device_record {
  struct usb_discovery *owner;
  struct usb_host_device *host;
  struct usb_bot storage;
  struct system_info_usb_device info;
  enum usb_speed speed;
  const char *detail;
  uint8_t hub_configuration;
  uint32_t hub_rx[USB_SSP_IDS], hub_tx[USB_SSP_IDS];
  bool hub_ssp;
  bool present, incomplete;
};

struct usb_discovery {
  struct usb_host_controller *host;
  struct usb_device_record *devices;
  struct system_info_usb_interface *interfaces;
  uint8_t *descriptors, *storage_scratch;
  struct usb_block_pool *storage_pool;
  size_t capacity, interface_count, device_count, registry_index;
  unsigned port_count, device_capacity;
  bool started, hardware_failed;
};

struct usb_controller_record {
  struct system_info_usb_controller info;
  struct usb_discovery *discovery;
  size_t pci_index, device_first, interface_first;
  bool pending;
};

/* Only the BSP prepares/mutates this registry. Readers acquire its final state
 * before touching records; no backing or published fact changes afterward. */
static struct {
  struct usb_controller_record *controllers;
  size_t controller_count, pending;
  struct system_info_usb info;
  atomic_uint_fast64_t state;
  bool prepared, incomplete;
} inventory = { .state = SYSTEM_INFO_USB_UNAVAILABLE };

static uint16_t read16(const uint8_t *bytes)
{
  return bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read32(const uint8_t *bytes)
{
  return read16(bytes) | ((uint32_t)read16(bytes + 2) << 16);
}

static bool bit_set(uint8_t *bits, unsigned index)
{
  uint8_t mask = 1u << (index % 8);
  bool previous = bits[index / 8] & mask;
  bits[index / 8] |= mask;
  return previous;
}

static uint64_t control_deadline(uint64_t overall)
{
  uint64_t control = task_deadline_after_ms(USB_CONTROL_TIMEOUT_MS);
  return control < overall ? control : overall;
}

static enum usb_result control(struct usb_device_record *device,
                               const struct usb_setup *setup, void *destination,
                               size_t capacity, size_t *bytes, uint64_t overall)
{
  *bytes = 0;
  uint64_t deadline = control_deadline(overall);
  if (task_deadline_expired(deadline)) {
    return USB_TIMEOUT;
  }
  struct usb_ticket ticket;
  enum usb_result result = usb_host_control_submit(device->host, setup, NULL, deadline, &ticket);
  if (result != USB_OK) {
    return result;
  }
  result = usb_host_control_wait(device->host, ticket, deadline);
  if (result != USB_OK) {
    usb_host_control_abandon(device->host, ticket);
    return result;
  }
  struct usb_completion completion;
  result = usb_host_control_take(device->host, ticket, destination, capacity, &completion);
  if (result != USB_OK) {
    usb_host_control_abandon(device->host, ticket);
    return result;
  }
  *bytes = completion.bytes;
  return completion.result;
}

static bool request_ok(struct usb_device_record *device, enum usb_result result)
{
  if (result == USB_OK) {
    return true;
  }
  device->incomplete = true;
  device->detail = "descriptor/control request failed";
  if (result == USB_IO || result == USB_TIMEOUT) {
    device->owner->hardware_failed = true;
  }
  return false;
}

static bool descriptor(struct usb_device_record *device, uint8_t type, uint8_t index,
                        uint16_t length, uint64_t deadline)
{
  struct usb_setup setup = {
    .request_type = USB_REQUEST_DEVICE_IN,
    .request = USB_REQUEST_GET_DESCRIPTOR,
    .value = ((uint16_t)type << 8) | index,
    .length = length,
  };
  size_t bytes;
  if (!request_ok(device, control(device, &setup, device->owner->descriptors,
                                 device->owner->capacity, &bytes, deadline))) {
    return false;
  }
  if (bytes != length) {
    device->incomplete = true;
    device->detail = "short descriptor";
    return false;
  }
  return true;
}

static void classify_class(struct usb_device_record *device, uint8_t class)
{
  if (class == USB_CLASS_HUB) {
    device->info.flags |= SYSTEM_INFO_USB_DEVICE_HUB;
  }
}

/* Ordinary hubs use their default interface. USB 2 multi-TT hubs retain the
 * required single-TT alternate, so no SET_INTERFACE is needed. */
static void select_hub_configuration(struct usb_device_record *device, size_t total)
{
  if (device->hub_configuration || device->info.device_class != USB_CLASS_HUB ||
      device->info.device_subclass || device->owner->descriptors[4] != 1) {
    return;
  }
  unsigned protocol;
  if (device->speed == USB_SPEED_FULL && device->info.device_protocol == 0) {
    protocol = 0;
  } else if (device->speed == USB_SPEED_HIGH &&
             (device->info.device_protocol == 1 || device->info.device_protocol == 2)) {
    protocol = device->info.device_protocol == 2 ? 1 : 0;
  } else if (usb_speed_is_enhanced(device->speed) && device->info.device_protocol == 3) {
    protocol = 0;
  } else {
    return;
  }
  bool candidate = false;
  for (size_t offset = USB_CONFIGURATION_BYTES; offset < total;
       offset += device->owner->descriptors[offset]) {
    const uint8_t *part = device->owner->descriptors + offset;
    if (part[1] == USB_DESCRIPTOR_INTERFACE) {
      candidate = !part[2] && !part[3] && part[4] == 1 && part[5] == USB_CLASS_HUB &&
                  !part[6] && part[7] == protocol;
    } else if (candidate && part[1] == USB_DESCRIPTOR_ENDPOINT) {
      if ((part[2] & USB_ENDPOINT_IN) &&
          (part[3] & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_INTERRUPT) {
        device->hub_configuration = device->owner->descriptors[5];
      }
      return;
    }
  }
}

/* The buffer is bounded, so rescanning earlier checked descriptors avoids a
 * 256-by-256 identity table on the 16 KiB kernel stack. */
static bool duplicate_interface(struct usb_device_record *device, size_t before,
                                uint8_t number, uint8_t alternate)
{
  for (size_t offset = USB_CONFIGURATION_BYTES; offset < before;
       offset += device->owner->descriptors[offset]) {
    const uint8_t *descriptor = device->owner->descriptors + offset;
    if (descriptor[1] == USB_DESCRIPTOR_INTERFACE &&
        descriptor[2] == number && descriptor[3] == alternate) {
      return true;
    }
  }
  return false;
}

static bool duplicate_endpoint(struct usb_device_record *device, size_t before,
                               uint8_t number, uint8_t alternate, uint8_t address)
{
  uint8_t previous_number = 0, previous_alternate = 0;
  for (size_t offset = USB_CONFIGURATION_BYTES; offset < before;
       offset += device->owner->descriptors[offset]) {
    const uint8_t *descriptor = device->owner->descriptors + offset;
    if (descriptor[1] == USB_DESCRIPTOR_INTERFACE) {
      previous_number = descriptor[2];
      previous_alternate = descriptor[3];
    } else if (descriptor[1] == USB_DESCRIPTOR_ENDPOINT && descriptor[2] == address &&
               (previous_number != number || previous_alternate == alternate)) {
      return true;
    }
  }
  return false;
}

static bool finish_interface(struct usb_device_record *device,
                             const struct system_info_usb_interface *interface)
{
  struct usb_discovery *discovery = device->owner;
  if (discovery->interface_count == USB_INTERFACE_BUDGET) {
    device->detail = "interface inventory exceeds reserved budget";
    return false;
  }
  discovery->interfaces[discovery->interface_count++] = *interface;
  classify_class(device, interface->class);
  return true;
}

static bool valid_endpoint(enum usb_speed speed, const uint8_t *endpoint)
{
  unsigned type = endpoint[3] & USB_ENDPOINT_TYPE_MASK;
  uint16_t wire_packet = read16(endpoint + 4), packet = wire_packet & USB_PACKET_SIZE_MASK;
  if ((wire_packet & USB_PACKET_RESERVED) ||
      ((wire_packet & USB_PACKET_TRANSACTIONS) &&
       (speed != USB_SPEED_HIGH || type == USB_ENDPOINT_BULK || type == USB_ENDPOINT_CONTROL)) ||
      (wire_packet & USB_PACKET_TRANSACTIONS) == USB_PACKET_TRANSACTIONS ||
      (!packet && type != USB_ENDPOINT_ISOCHRONOUS)) {
    return false;
  }
  if ((type == USB_ENDPOINT_CONTROL || type == USB_ENDPOINT_BULK) && (endpoint[3] & ~USB_ENDPOINT_TYPE_MASK)) {
    return false;
  }
  if (type == USB_ENDPOINT_INTERRUPT) {
    unsigned usage = endpoint[3] >> USB_ENDPOINT_USAGE_SHIFT;
    if ((endpoint[3] & USB_ENDPOINT_INTERRUPT_RESERVED) ||
        (usb_speed_is_enhanced(speed) ? usage > 1 : usage != 0)) {
      return false;
    }
    if (!endpoint[6] || ((usb_speed_is_enhanced(speed) || speed == USB_SPEED_HIGH) && endpoint[6] > 16) ||
        (usb_speed_is_enhanced(speed) && usage == 1 && endpoint[6] < 8)) {
      return false;
    }
  }
  if (type == USB_ENDPOINT_ISOCHRONOUS &&
      ((endpoint[3] & USB_ENDPOINT_ISO_RESERVED) ||
       (endpoint[3] & USB_ENDPOINT_ISO_USAGE_MASK) == USB_ENDPOINT_ISO_USAGE_MASK ||
       !endpoint[6] || endpoint[6] > 16)) {
    return false;
  }
  switch (speed) {
  case USB_SPEED_LOW:
    return (type == USB_ENDPOINT_CONTROL && packet == 8) ||
           (type == USB_ENDPOINT_INTERRUPT && packet <= 8);
  case USB_SPEED_FULL:
    if (type == USB_ENDPOINT_BULK || type == USB_ENDPOINT_CONTROL) {
      return packet == 8 || packet == 16 || packet == 32 || packet == 64;
    }
    return packet <= (type == USB_ENDPOINT_INTERRUPT ? 64 : 1023);
  case USB_SPEED_HIGH:
    if (type == USB_ENDPOINT_BULK || type == USB_ENDPOINT_CONTROL) {
      return packet == (type == USB_ENDPOINT_BULK ? 512 : 64);
    }
    return packet <= 1024;
  case USB_SPEED_SUPER:
  case USB_SPEED_SUPER_PLUS:
    if (type == USB_ENDPOINT_BULK || type == USB_ENDPOINT_CONTROL) {
      return packet == (type == USB_ENDPOINT_BULK ? 1024 : 512);
    }
    return packet <= 1024;
  default:
    return false;
  }
}

static bool valid_companion(const uint8_t *endpoint, const uint8_t *companion)
{
  unsigned type = endpoint[3] & USB_ENDPOINT_TYPE_MASK;
  unsigned burst = companion[2], attributes = companion[3], interval = read16(companion + 4);
  unsigned packet = read16(endpoint + 4) & USB_PACKET_SIZE_MASK;
  if (burst > 15) {
    return false;
  }
  if (type == USB_ENDPOINT_BULK) {
    return !(attributes & USB_COMPANION_BULK_RESERVED) &&
           (attributes & USB_COMPANION_STREAMS_MASK) <= 16 && !interval;
  }
  if (type == USB_ENDPOINT_CONTROL) {
    return !burst && !attributes && !interval;
  }
  if ((burst && packet != 1024) || (type == USB_ENDPOINT_INTERRUPT && attributes)) {
    return false;
  }
  unsigned mult = 0;
  if (type == USB_ENDPOINT_ISOCHRONOUS) {
    if (attributes & USB_COMPANION_ISO_RESERVED) {
      return false;
    }
    if (attributes & USB_COMPANION_SSP_ISO) {
      return interval == 1;
    }
    mult = attributes & USB_COMPANION_ISO_MULT_MASK;
    if (mult > 2 || (!burst && mult)) {
      return false;
    }
  }
  return interval <= packet * (burst + 1) * (mult + 1);
}

static bool parse_configuration(struct usb_device_record *device, size_t total)
{
  const uint8_t *header = device->owner->descriptors;
  uint8_t interfaces = header[4], configuration = header[5];
  uint8_t numbers[USB_BITSET_BYTES] = {0}, default_alternates[USB_BITSET_BYTES] = {0};
  struct system_info_usb_interface interface = { .configuration = configuration };
  unsigned unique_interfaces = 0, endpoints = 0;
  bool have_interface = false;
  uint8_t companion_pending = 0;
  const uint8_t *last_endpoint = NULL;

  for (size_t offset = USB_CONFIGURATION_BYTES; offset < total;) {
    if (total - offset < 2) {
      return false;
    }
    const uint8_t *part = device->owner->descriptors + offset;
    size_t length = part[0];
    if (length < 2 || length > total - offset ||
        (companion_pending && part[1] != companion_pending)) {
      return false;
    }
    switch (part[1]) {
    case USB_DESCRIPTOR_INTERFACE:
      if (length != USB_INTERFACE_BYTES || part[2] >= interfaces ||
          duplicate_interface(device, offset, part[2], part[3])) {
        return false;
      }
      if (have_interface) {
        if (endpoints != interface.endpoint_count) {
          return false;
        }
        if (!finish_interface(device, &interface)) {
          return false;
        }
      }
      memset(&interface, 0, sizeof(interface));
      interface.configuration = configuration;
      interface.number = part[2];
      interface.alternate = part[3];
      interface.endpoint_count = part[4];
      interface.class = part[5];
      interface.subclass = part[6];
      interface.protocol = part[7];
      if (!bit_set(numbers, interface.number)) {
        ++unique_interfaces;
      }
      if (!interface.alternate) {
        bit_set(default_alternates, interface.number);
      }
      endpoints = 0;
      have_interface = true;
      break;
    case USB_DESCRIPTOR_ENDPOINT: {
      if (!have_interface || length < USB_ENDPOINT_BYTES ||
          endpoints >= interface.endpoint_count || !(part[2] & USB_ENDPOINT_NUMBER_MASK) ||
          (part[2] & USB_ENDPOINT_ADDRESS_RESERVED) ||
          duplicate_endpoint(device, offset, interface.number, interface.alternate, part[2]) ||
          !valid_endpoint(device->speed, part)) {
        return false;
      }
      ++endpoints;
      last_endpoint = part;
      companion_pending = usb_speed_is_enhanced(device->speed) ? USB_DESCRIPTOR_SUPER_COMPANION : 0;
      break;
    }
    case USB_DESCRIPTOR_SUPER_COMPANION:
      if (companion_pending != USB_DESCRIPTOR_SUPER_COMPANION || length != USB_SUPER_COMPANION_BYTES ||
          !valid_companion(last_endpoint, part)) {
        return false;
      }
      companion_pending =
        (last_endpoint[3] & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_ISOCHRONOUS &&
        (part[3] & USB_COMPANION_SSP_ISO) ? USB_DESCRIPTOR_SSP_ISO_COMPANION : 0;
      break;
    case USB_DESCRIPTOR_SSP_ISO_COMPANION:
      if (companion_pending != USB_DESCRIPTOR_SSP_ISO_COMPANION || length != 8 || read16(part + 2)) {
        return false;
      }
      companion_pending = 0;
      break;
    case USB_DESCRIPTOR_INTERFACE_ASSOCIATION:
      if (length != 8 || !part[3] || part[2] >= interfaces || part[3] > interfaces - part[2]) {
        return false;
      }
      classify_class(device, part[4]);
      break;
    case USB_DESCRIPTOR_DEVICE:
    case USB_DESCRIPTOR_CONFIGURATION:
      return false;
    default:
      /* Class/vendor descriptors stay opaque; the checked length still bounds
       * traversal. They cannot replace standard interface/endpoint facts. */
      break;
    }
    offset += length;
  }
  if (!have_interface || companion_pending || endpoints != interface.endpoint_count ||
      unique_interfaces != interfaces || memcmp(numbers, default_alternates, sizeof(numbers))) {
    return false;
  }
  return finish_interface(device, &interface);
}

static bool valid_packet(enum usb_speed speed, uint8_t wire, uint16_t *packet)
{
  switch (speed) {
  case USB_SPEED_LOW:
    *packet = 8;
    return wire == 8;
  case USB_SPEED_FULL:
    *packet = wire;
    return wire == 8 || wire == 16 || wire == 32 || wire == 64;
  case USB_SPEED_HIGH:
    *packet = 64;
    return wire == 64;
  case USB_SPEED_SUPER:
  case USB_SPEED_SUPER_PLUS:
    *packet = 512;
    return wire == 9;
  default:
    return false;
  }
}

static void inspect_device(struct usb_device_record *device, uint64_t deadline)
{
  device->speed = usb_host_device_speed(device->host);
  enum usb_result address = usb_host_address(device->host, deadline);
  if (address == USB_UNSUPPORTED && device->speed == USB_SPEED_UNKNOWN) {
    device->incomplete = true;
    device->detail = "unsupported device speed";
    return;
  }
  if (!request_ok(device, address) ||
      !descriptor(device, USB_DESCRIPTOR_DEVICE, 0, USB_DEVICE_PREFIX_BYTES, deadline)) {
    return;
  }
  uint8_t prefix[USB_DEVICE_PREFIX_BYTES];
  memcpy(prefix, device->owner->descriptors, sizeof(prefix));
  uint16_t packet;
  if (prefix[0] != USB_DEVICE_BYTES || prefix[1] != USB_DESCRIPTOR_DEVICE ||
      !valid_packet(device->speed, prefix[7], &packet)) {
    device->incomplete = true;
    device->detail = "invalid device descriptor prefix";
    return;
  }
  if (device->speed == USB_SPEED_FULL && packet != 8 &&
      !request_ok(device, usb_host_update_packet(device->host, packet, deadline))) {
    return;
  }
  if (!descriptor(device, USB_DESCRIPTOR_DEVICE, 0, USB_DEVICE_BYTES, deadline)) {
    return;
  }
  const uint8_t *description = device->owner->descriptors;
  if (memcmp(prefix, description, sizeof(prefix)) || !description[17]) {
    device->incomplete = true;
    device->detail = "invalid or changing device descriptor";
    return;
  }
  device->info.vendor_id = read16(description + 8);
  device->info.product_id = read16(description + 10);
  device->info.device_class = description[4];
  device->info.device_subclass = description[5];
  device->info.device_protocol = description[6];
  device->info.configuration_count = description[17];
  device->info.flags |= SYSTEM_INFO_USB_DEVICE_IDENTIFIED;
  classify_class(device, device->info.device_class);

  uint8_t configurations[USB_BITSET_BYTES] = {0};
  for (unsigned index = 0; index < device->info.configuration_count; ++index) {
    if (!descriptor(device, USB_DESCRIPTOR_CONFIGURATION, index, USB_CONFIGURATION_BYTES, deadline)) {
      if (device->owner->hardware_failed) {
        return;
      }
      continue;
    }
    uint8_t header[USB_CONFIGURATION_BYTES];
    memcpy(header, device->owner->descriptors, sizeof(header));
    size_t total = read16(header + 2);
    if (header[0] != USB_CONFIGURATION_BYTES || header[1] != USB_DESCRIPTOR_CONFIGURATION ||
        total < USB_CONFIGURATION_BYTES || !header[4] || !header[5] ||
        !(header[7] & USB_CONFIGURATION_REQUIRED) || (header[7] & USB_CONFIGURATION_RESERVED) ||
        bit_set(configurations, header[5])) {
      device->incomplete = true;
      device->detail = "invalid or duplicate configuration header";
      continue;
    }
    if (total > device->owner->capacity) {
      device->incomplete = true;
      device->detail = "configuration exceeds reserved descriptor budget";
      continue;
    }
    if (!descriptor(device, USB_DESCRIPTOR_CONFIGURATION, index, total, deadline)) {
      if (device->owner->hardware_failed) {
        return;
      }
      continue;
    }
    size_t previous_interfaces = device->owner->interface_count;
    uint8_t previous_flags = device->info.flags;
    if (memcmp(header, device->owner->descriptors, sizeof(header)) || !parse_configuration(device, total)) {
      device->owner->interface_count = previous_interfaces;
      device->info.flags = previous_flags;
      device->incomplete = true;
      device->detail = "malformed or changing configuration descriptors";
    } else {
      select_hub_configuration(device, total);
      device->storage.host = device->host;
      usb_bot_select(&device->storage, device->owner->descriptors, total, device->speed);
    }
  }
}

/* With BSP interrupts disabled, no controller worker can interleave this
 * final pass. Each pending entry has already stopped writing its records. */
static void publish_inventory(void)
{
  if (inventory.pending ||
      atomic_load_explicit(&inventory.state, memory_order_relaxed) != SYSTEM_INFO_USB_INITIALIZING) {
    return;
  }
  size_t device_index = 0, interface_index = 0;
  for (size_t index = 0; index < inventory.controller_count; ++index) {
    struct usb_controller_record *controller = &inventory.controllers[index];
    struct usb_discovery *discovery = controller->discovery;
    controller->device_first = device_index;
    controller->interface_first = interface_index;
    if (!discovery) {
      continue;
    }
    for (unsigned port = 0; port < discovery->device_count; ++port) {
      struct usb_device_record *device = &discovery->devices[port];
      if (!device->present) {
        continue;
      }
      device->info.controller_index = index;
      if (device->info.parent_index != SYSTEM_INFO_USB_NO_PARENT) {
        device->info.parent_index += controller->device_first;
      }
      size_t first = device->info.interface_first;
      for (size_t i = 0; i < device->info.interface_count; ++i) {
        discovery->interfaces[first + i].device_index = device_index;
      }
      device->info.interface_first += interface_index;
      ++device_index;
    }
    interface_index += discovery->interface_count;
  }
  inventory.info = (struct system_info_usb) {
    .state = inventory.incomplete ? SYSTEM_INFO_USB_INCOMPLETE : SYSTEM_INFO_USB_COMPLETE,
    .controller_count = inventory.controller_count,
    .device_count = device_index,
    .interface_count = interface_index,
  };
  atomic_store_explicit(&inventory.state, inventory.info.state, memory_order_release);
}

static bool is_usb_controller(const struct pci_device *device)
{
  return device->base_class == PCI_CLASS_SERIAL_BUS && device->subclass == PCI_SUBCLASS_USB;
}

static void copy_pci_identity(struct system_info_pci_function *reply,
                              const struct pci_device *device)
{
  *reply = (struct system_info_pci_function) {
    .bus = device->address.bus,
    .device = device->address.device,
    .function = device->address.function,
    .header_type = device->header_type,
    .vendor_id = device->vendor_id,
    .device_id = device->device_id,
    .base_class = device->base_class,
    .subclass = device->subclass,
    .interface = device->interface,
    .revision = device->revision,
  };
}

void usb_inventory_prepare(void)
{
  if (inventory.prepared) {
    return;
  }
  inventory.prepared = true;
  if (pci_inventory_state() == PCI_INVENTORY_UNAVAILABLE) {
    return;
  }
  atomic_store_explicit(&inventory.state, SYSTEM_INFO_USB_INITIALIZING, memory_order_release);
  inventory.incomplete = pci_inventory_state() != PCI_INVENTORY_COMPLETE;
  size_t count = 0;
  for (size_t i = 0; i < pci_device_count(); ++i) {
    if (is_usb_controller(pci_device_at(i))) {
      ++count;
    }
  }
  if (count) {
    if (count <= SIZE_MAX / sizeof(*inventory.controllers)) {
      inventory.controllers = kmalloc(count * sizeof(*inventory.controllers));
    }
    if (!inventory.controllers) {
      inventory.incomplete = true;
      for (size_t i = 0; i < pci_device_count(); ++i) {
        const struct pci_device *device = pci_device_at(i);
        if (is_usb_controller(device)) {
          klog("usb: %x:%x.%u registry allocation failed; controller not retained\n",
               device->address.bus, device->address.device, device->address.function);
        }
      }
      publish_inventory();
      return;
    }
    memset(inventory.controllers, 0, count * sizeof(*inventory.controllers));
  }
  for (size_t i = 0; i < pci_device_count(); ++i) {
    const struct pci_device *device = pci_device_at(i);
    if (!is_usb_controller(device)) {
      continue;
    }
    struct usb_controller_record *controller =
      &inventory.controllers[inventory.controller_count++];
    copy_pci_identity(&controller->info.pci, device);
    controller->pci_index = i;
    if (device->interface == PCI_INTERFACE_XHCI) {
      controller->pending = true;
      ++inventory.pending;
    } else {
      controller->info.state = SYSTEM_INFO_USB_CONTROLLER_UNSUPPORTED;
      inventory.incomplete = true;
      klog("usb: %x:%x.%u host interface %x unsupported\n", device->address.bus,
           device->address.device, device->address.function, device->interface);
    }
  }
  publish_inventory();
}

size_t usb_inventory_controller_count(void)
{
  return inventory.controller_count;
}

size_t usb_inventory_pci_index(size_t index)
{
  return index < inventory.controller_count ? inventory.controllers[index].pci_index : SIZE_MAX;
}

struct usb_discovery *usb_prepare(struct usb_host_controller *host, size_t index)
{
  if (!host || index >= inventory.controller_count) {
    return NULL;
  }
  struct usb_controller_record *controller = &inventory.controllers[index];
  if (!controller->pending || controller->discovery) {
    return NULL;
  }
  unsigned ports = usb_host_port_count(host);
  unsigned descendants = usb_host_descendant_capacity(host);
  size_t capacity = usb_host_control_capacity();
  controller->info.root_port_count = ports;
  if (!ports || ports > UINT16_MAX || descendants > UINT_MAX - ports ||
      sizeof(struct usb_device_record) > SIZE_MAX / (ports + descendants) ||
      capacity < USB_DEVICE_BYTES || capacity > UINT16_MAX) {
    return NULL;
  }
  struct usb_discovery *discovery = kmalloc(sizeof(*discovery));
  if (!discovery) {
    return NULL;
  }
  memset(discovery, 0, sizeof(*discovery));
  discovery->registry_index = index;
  discovery->host = host;
  discovery->port_count = ports;
  discovery->device_capacity = ports + descendants;
  discovery->capacity = capacity;
  discovery->devices = kmalloc(discovery->device_capacity * sizeof(*discovery->devices));
  discovery->descriptors = kmalloc(capacity);
  discovery->storage_scratch = kmalloc(USB_BULK_BYTES);
  discovery->storage_pool = usb_block_prepare(host, discovery->device_capacity);
  discovery->interfaces = kmalloc(USB_INTERFACE_BUDGET * sizeof(*discovery->interfaces));
  if (!discovery->devices || !discovery->descriptors || !discovery->storage_scratch || !discovery->interfaces) {
    usb_release_prepared(discovery);
    return NULL;
  }
  memset(discovery->devices, 0, discovery->device_capacity * sizeof(*discovery->devices));
  controller->discovery = discovery;
  return discovery;
}

void usb_release_prepared(struct usb_discovery *discovery)
{
  if (!discovery || discovery->started) {
    return;
  }
  struct usb_controller_record *controller = &inventory.controllers[discovery->registry_index];
  if (!controller->pending) {
    return;
  }
  if (controller->discovery == discovery) {
    controller->discovery = NULL;
  }
  kfree(discovery->interfaces);
  usb_block_release_prepared(discovery->storage_pool);
  kfree(discovery->storage_scratch);
  kfree(discovery->descriptors);
  kfree(discovery->devices);
  kfree(discovery);
}

static uint8_t observation_speed(enum usb_speed speed)
{
  switch (speed) {
  case USB_SPEED_LOW: return SYSTEM_INFO_USB_SPEED_LOW;
  case USB_SPEED_FULL: return SYSTEM_INFO_USB_SPEED_FULL;
  case USB_SPEED_HIGH: return SYSTEM_INFO_USB_SPEED_HIGH;
  case USB_SPEED_SUPER: return SYSTEM_INFO_USB_SPEED_SUPER;
  case USB_SPEED_SUPER_PLUS: return SYSTEM_INFO_USB_SPEED_SUPER_PLUS;
  default: return SYSTEM_INFO_USB_SPEED_UNKNOWN;
  }
}

static bool hub_request(struct usb_device_record *device, const struct usb_setup *setup,
                        void *destination, size_t length, uint64_t deadline)
{
  size_t bytes;
  if (!request_ok(device, control(device, setup, destination, length, &bytes, deadline))) {
    return false;
  }
  if (bytes != length) {
    device->incomplete = true;
    device->detail = "short hub response";
    return false;
  }
  return true;
}

static bool hub_feature(struct usb_device_record *hub, unsigned port, unsigned feature,
                        bool set, uint64_t deadline)
{
  struct usb_setup setup = {
    .request_type = USB_REQUEST_PORT_OUT,
    .request = set ? USB_REQUEST_SET_FEATURE : USB_REQUEST_CLEAR_FEATURE,
    .value = feature,
    .index = port,
  };
  return hub_request(hub, &setup, NULL, 0, deadline);
}

static unsigned hub_power_bit(const struct usb_device_record *hub)
{
  return usb_speed_is_enhanced(hub->speed) ? USB_SUPER_PORT_POWER : USB_HUB_PORT_POWER;
}

static uint64_t hub_lane_rate(uint32_t attribute)
{
  uint64_t rate = attribute >> USB_SSP_MANTISSA_SHIFT;
  unsigned exponent = (attribute >> USB_SSP_EXPONENT_SHIFT) & USB_SSP_EXPONENT_MASK;
  while (exponent--) {
    rate *= 1000;
  }
  return rate;
}

static bool parse_hub_ssp(struct usb_device_record *hub, const uint8_t *part, unsigned length)
{
  if (hub->hub_ssp || length < USB_SSP_CAPABILITY_BYTES || part[3] || read16(part + 10)) {
    return false;
  }
  uint32_t attributes = read32(part + 4);
  unsigned count = (attributes & USB_SSP_ATTRIBUTE_COUNT_MASK) + 1;
  unsigned ids = ((attributes >> USB_SSP_ID_COUNT_SHIFT) & USB_SSP_ID_COUNT_MASK) + 1;
  if ((attributes & USB_SSP_ATTRIBUTES_RESERVED) || (count & 1) ||
      length != USB_SSP_CAPABILITY_BYTES + count * sizeof(uint32_t) ||
      (read16(part + 8) & USB_SSP_FUNCTION_RESERVED)) {
    return false;
  }
  unsigned seen = 0, unique = 0;
  for (unsigned i = 0; i < count; i += 2) {
    uint32_t rx = read32(part + USB_SSP_CAPABILITY_BYTES + i * sizeof(uint32_t));
    uint32_t tx = read32(part + USB_SSP_CAPABILITY_BYTES + (i + 1) * sizeof(uint32_t));
    unsigned id = rx & USB_SSP_ID_MASK;
    unsigned rx_protocol = (rx >> USB_SSP_PROTOCOL_SHIFT) & USB_SSP_PROTOCOL_MASK;
    unsigned tx_protocol = (tx >> USB_SSP_PROTOCOL_SHIFT) & USB_SSP_PROTOCOL_MASK;
    if ((seen & (1u << id)) || (rx & USB_SSP_TRANSMIT) || !(tx & USB_SSP_TRANSMIT) ||
        (tx & USB_SSP_ID_MASK) != id || (rx & USB_SSP_RESERVED) || (tx & USB_SSP_RESERVED) ||
        ((rx ^ tx) & USB_SSP_ASYMMETRIC) ||
        rx_protocol > USB_SSP_PROTOCOL_SUPER_PLUS || tx_protocol > USB_SSP_PROTOCOL_SUPER_PLUS ||
        (!(rx & USB_SSP_ASYMMETRIC) &&
         (hub_lane_rate(rx) != hub_lane_rate(tx) || rx_protocol != tx_protocol)) ||
        !(rx >> USB_SSP_MANTISSA_SHIFT) || !(tx >> USB_SSP_MANTISSA_SHIFT)) {
      return false;
    }
    seen |= 1u << id;
    ++unique;
    hub->hub_rx[id] = rx;
    hub->hub_tx[id] = tx;
  }
  if (unique != ids || !(seen & (1u << (read16(part + 8) & USB_SSP_ID_MASK)))) {
    return false;
  }
  hub->hub_ssp = true;
  return true;
}

static bool inspect_hub_bos(struct usb_device_record *hub, uint64_t deadline)
{
  if (!descriptor(hub, USB_DESCRIPTOR_BOS, 0, USB_BOS_BYTES, deadline)) {
    return false;
  }
  uint8_t prefix[USB_BOS_BYTES];
  memcpy(prefix, hub->owner->descriptors, sizeof(prefix));
  size_t total = read16(prefix + 2);
  if (prefix[0] != USB_BOS_BYTES || prefix[1] != USB_DESCRIPTOR_BOS ||
      total < USB_BOS_BYTES || total > hub->owner->capacity) {
    hub->detail = "invalid or oversized hub BOS";
    return false;
  }
  if (!descriptor(hub, USB_DESCRIPTOR_BOS, 0, total, deadline)) {
    return false;
  }
  if (memcmp(prefix, hub->owner->descriptors, sizeof(prefix))) {
    hub->detail = "changing hub BOS";
    return false;
  }
  unsigned count = 0;
  bool super = false;
  for (size_t offset = USB_BOS_BYTES; offset < total;) {
    const uint8_t *part = hub->owner->descriptors + offset;
    if (total - offset < 3 || part[0] < 3 || part[0] > total - offset ||
        part[1] != USB_DESCRIPTOR_CAPABILITY ||
        (part[2] == USB_CAPABILITY_SUPER_PLUS && !parse_hub_ssp(hub, part, part[0]))) {
      hub->detail = "invalid hub BOS capability";
      return false;
    }
    if (part[2] == USB_CAPABILITY_SUPER) {
      if (super || part[0] != USB_SUPER_CAPABILITY_BYTES ||
          !(read16(part + 4) & USB_SUPER_CAPABILITY_SPEED)) {
        hub->detail = "invalid hub SuperSpeed capability";
        return false;
      }
      super = true;
    }
    ++count;
    offset += part[0];
  }
  if (!super || count != prefix[4] || (hub->speed == USB_SPEED_SUPER_PLUS && !hub->hub_ssp)) {
    hub->detail = "missing or inconsistent hub capabilities";
    return false;
  }
  return true;
}

static bool hub_port_status(struct usb_device_record *hub, unsigned port, uint16_t *status,
                            uint16_t *change, uint64_t deadline)
{
  struct usb_setup setup = {
    .request_type = USB_REQUEST_PORT_IN,
    .request = USB_REQUEST_GET_STATUS,
    .index = port,
    .length = USB_HUB_STATUS_BYTES,
  };
  uint8_t bytes[USB_HUB_STATUS_BYTES];
  if (!hub_request(hub, &setup, bytes, sizeof(bytes), deadline)) {
    return false;
  }
  *status = read16(bytes);
  *change = read16(bytes + 2);
  bool enhanced = usb_speed_is_enhanced(hub->speed);
  if ((*status & (enhanced ? USB_SUPER_PORT_RESERVED : USB_HUB_PORT_RESERVED)) ||
      (*change & (enhanced ? USB_SUPER_CHANGE_RESERVED : USB_HUB_CHANGE_RESERVED)) ||
      (enhanced && ((*status >> USB_SUPER_LINK_SHIFT) & USB_SUPER_LINK_MASK) >= USB_SUPER_LINK_RESERVED_FIRST)) {
    hub->incomplete = true;
    hub->detail = "invalid hub port status";
    return false;
  }
  return true;
}

static bool hub_sleep(unsigned milliseconds, uint64_t deadline)
{
  uint64_t wake = task_deadline_after_ms(milliseconds);
  if (wake > deadline) {
    return false;
  }
  kernel_task_sleep_until(wake);
  return !task_deadline_expired(deadline);
}

static bool hub_acknowledge(struct usb_device_record *hub, unsigned port, uint16_t change,
                            uint64_t deadline)
{
  for (unsigned bit = 0; bit < (usb_speed_is_enhanced(hub->speed) ? 8u : 5u); ++bit) {
    unsigned feature = USB_HUB_FEATURE_CHANGE_FIRST + bit;
    if (usb_speed_is_enhanced(hub->speed)) {
      if (bit == 5) {
        feature = USB_SUPER_FEATURE_CHANGE_BH_RESET;
      }
      if (bit == 6) {
        feature = USB_SUPER_FEATURE_CHANGE_LINK;
      }
      if (bit == 7) {
        feature = USB_SUPER_FEATURE_CHANGE_CONFIG_ERROR;
      }
    }
    if ((change & (1u << bit)) &&
        !hub_feature(hub, port, feature, false, deadline)) {
      return false;
    }
  }
  return true;
}

static bool hub_port_failed(const struct usb_device_record *hub, uint16_t status, uint16_t change)
{
  if (!usb_speed_is_enhanced(hub->speed)) {
    return false;
  }
  unsigned state = (status >> USB_SUPER_LINK_SHIFT) & USB_SUPER_LINK_MASK;
  return (change & USB_SUPER_CHANGE_CONFIG_ERROR) || state == USB_SUPER_LINK_INACTIVE ||
    state == USB_SUPER_LINK_COMPLIANCE || state == USB_SUPER_LINK_LOOPBACK;
}

static bool hub_port_connected(const struct usb_device_record *hub, uint16_t status, uint16_t change)
{
  return (status & (USB_HUB_PORT_CONNECTION | hub_power_bit(hub))) ==
         (USB_HUB_PORT_CONNECTION | hub_power_bit(hub)) &&
         !(status & (USB_HUB_PORT_OVERCURRENT |
           (usb_speed_is_enhanced(hub->speed) ? 0 : USB_HUB_PORT_SUSPEND))) &&
         !(change & USB_HUB_CHANGE_CONNECTION);
}

static enum usb_speed hub_reset_port(struct usb_device_record *hub, unsigned port,
                                     uint64_t deadline)
{
  uint64_t stable = task_deadline_after_ms(USB_HUB_DEBOUNCE_MS);
  uint16_t status, change;
  do {
    if (!hub_sleep(USB_HUB_POLL_MS, deadline) ||
        !hub_port_status(hub, port, &status, &change, deadline) ||
        !hub_port_connected(hub, status, change)) {
      return USB_SPEED_UNKNOWN;
    }
  } while (!task_deadline_expired(stable));

  if (!hub_acknowledge(hub, port, change, deadline) ||
      !hub_feature(hub, port, USB_HUB_FEATURE_RESET, true, deadline)) {
    return USB_SPEED_UNKNOWN;
  }
  uint64_t reset = task_deadline_after_ms(USB_HUB_RESET_TIMEOUT_MS);
  if (reset > deadline) {
    reset = deadline;
  }
  do {
    if (!hub_sleep(USB_HUB_POLL_MS, reset) ||
        !hub_port_status(hub, port, &status, &change, reset) ||
        !hub_port_connected(hub, status, change)) {
      return USB_SPEED_UNKNOWN;
    }
  } while ((status & USB_HUB_PORT_RESET) || !(status & USB_HUB_PORT_ENABLE) ||
           !(change & USB_HUB_CHANGE_RESET));
  if (!hub_acknowledge(hub, port, change, deadline) ||
      !hub_sleep(USB_HUB_RESET_RECOVERY_MS, deadline) ||
      !hub_port_status(hub, port, &status, &change, deadline) ||
      !hub_port_connected(hub, status, change) || !(status & USB_HUB_PORT_ENABLE)) {
    return USB_SPEED_UNKNOWN;
  }
  if (usb_speed_is_enhanced(hub->speed)) {
    return ((status >> USB_SUPER_LINK_SHIFT) & USB_SUPER_LINK_MASK) == 0 &&
      !(change & USB_SUPER_CHANGE_CONFIG_ERROR) ? USB_SPEED_SUPER : USB_SPEED_UNKNOWN;
  }
  if ((status & (USB_HUB_PORT_LOW_SPEED | USB_HUB_PORT_HIGH_SPEED)) ==
      (USB_HUB_PORT_LOW_SPEED | USB_HUB_PORT_HIGH_SPEED)) {
    return USB_SPEED_UNKNOWN;
  }
  if (status & USB_HUB_PORT_LOW_SPEED) {
    return USB_SPEED_LOW;
  }
  return status & USB_HUB_PORT_HIGH_SPEED ? USB_SPEED_HIGH : USB_SPEED_FULL;
}

static bool hub_child_link(struct usb_device_record *hub, unsigned port, struct usb_link *link,
                           uint64_t deadline)
{
  if (!usb_speed_is_enhanced(hub->speed)) {
    return true;
  }
  if (!hub->hub_ssp) {
    *link = (struct usb_link){ .speed = USB_SPEED_SUPER,
      .rx_bps = USB_SUPER_LANE_BPS, .tx_bps = USB_SUPER_LANE_BPS, .rx_lanes = 1, .tx_lanes = 1 };
    return true;
  }
  struct usb_setup setup = {
    .request_type = USB_REQUEST_PORT_IN,
    .request = USB_REQUEST_GET_STATUS,
    .value = USB_SUPER_EXT_STATUS,
    .index = port,
    .length = USB_SUPER_EXT_STATUS_BYTES,
  };
  uint8_t bytes[USB_SUPER_EXT_STATUS_BYTES];
  if (!hub_request(hub, &setup, bytes, sizeof(bytes), deadline)) {
    return false;
  }
  uint16_t status = read16(bytes), change = read16(bytes + 2);
  uint32_t extended = read32(bytes + 4);
  if (!hub_port_connected(hub, status, change) || !(status & USB_HUB_PORT_ENABLE) ||
      (status & (USB_SUPER_PORT_RESERVED | USB_HUB_PORT_RESET)) ||
      (change & (USB_SUPER_CHANGE_RESERVED | USB_SUPER_CHANGE_CONFIG_ERROR)) ||
      ((status >> USB_SUPER_LINK_SHIFT) & USB_SUPER_LINK_MASK) ||
      (extended & USB_SUPER_EXT_RESERVED)) {
    return false;
  }
  uint32_t rx = hub->hub_rx[extended & USB_SSP_ID_MASK];
  uint32_t tx = hub->hub_tx[(extended >> USB_SUPER_EXT_TX_ID_SHIFT) & USB_SSP_ID_MASK];
  unsigned rx_protocol = (rx >> USB_SSP_PROTOCOL_SHIFT) & USB_SSP_PROTOCOL_MASK;
  unsigned tx_protocol = (tx >> USB_SSP_PROTOCOL_SHIFT) & USB_SSP_PROTOCOL_MASK;
  unsigned rx_lanes = ((extended >> USB_SUPER_EXT_RX_LANES_SHIFT) & USB_SSP_ID_MASK) + 1;
  unsigned tx_lanes = ((extended >> USB_SUPER_EXT_TX_LANES_SHIFT) & USB_SSP_ID_MASK) + 1;
  uint64_t rx_bps = hub_lane_rate(rx), tx_bps = hub_lane_rate(tx);
  if (!rx_bps || rx_bps != tx_bps || rx_lanes != tx_lanes ||
      (rx & USB_SSP_ASYMMETRIC) || (tx & USB_SSP_ASYMMETRIC) || rx_protocol != tx_protocol ||
      (rx_protocol != USB_SSP_PROTOCOL_SUPER && rx_protocol != USB_SSP_PROTOCOL_SUPER_PLUS)) {
    return false;
  }
  *link = (struct usb_link){ .rx_bps = rx_bps * rx_lanes, .tx_bps = tx_bps * tx_lanes,
    .rx_lanes = rx_lanes, .tx_lanes = tx_lanes,
    .speed = rx_protocol == USB_SSP_PROTOCOL_SUPER ? USB_SPEED_SUPER : USB_SPEED_SUPER_PLUS };
  return true;
}

static void inspect_record(struct usb_device_record *device, uint64_t deadline)
{
  device->info.interface_first = device->owner->interface_count;
  if (!device->host || device->owner->hardware_failed || task_deadline_expired(deadline)) {
    device->incomplete = true;
    device->detail = "connected port could not be inspected";
  } else {
    inspect_device(device, deadline);
  }
  device->info.interface_count = device->owner->interface_count - device->info.interface_first;
}

static bool read_usb3_hub(struct usb_device_record *hub, unsigned *ports,
                          unsigned *power_delay, uint64_t deadline)
{
  if (usb_host_device_depth(hub->host) > USB_SUPER_HUB_MAX_DEPTH ||
      !inspect_hub_bos(hub, deadline)) {
    if (!hub->detail) {
      hub->detail = "USB 3 hub depth unsupported";
    }
    return false;
  }
  struct usb_setup descriptor = {
    .request_type = USB_REQUEST_HUB_IN,
    .request = USB_REQUEST_GET_DESCRIPTOR,
    .value = USB_DESCRIPTOR_SUPER_HUB << 8,
    .length = USB_SUPER_HUB_BYTES,
  };
  uint8_t bytes[USB_SUPER_HUB_BYTES];
  if (!hub_request(hub, &descriptor, bytes, sizeof(bytes), deadline)) {
    return false;
  }
  *ports = bytes[2];
  unsigned characteristics = read16(bytes + 3);
  unsigned removable = read16(bytes + 10);
  if (bytes[0] != USB_SUPER_HUB_BYTES || bytes[1] != USB_DESCRIPTOR_SUPER_HUB ||
      !*ports || *ports > USB_SUPER_HUB_MAX_PORTS ||
      (characteristics & USB_SUPER_HUB_CHARACTERISTICS_RESERVED) ||
      (characteristics & USB_HUB_POWER_SWITCH_MASK) > 1 ||
      bytes[7] > USB_SUPER_HUB_HEADER_LATENCY_MAX ||
      (removable & ~(((1u << (*ports + 1)) - 1) & ~1u))) {
    hub->detail = "invalid USB 3 hub descriptor";
    return false;
  }
  *power_delay = bytes[5] * 2;
  struct usb_setup depth = {
    .request_type = USB_REQUEST_HUB_OUT,
    .request = USB_REQUEST_SET_HUB_DEPTH,
    .value = usb_host_device_depth(hub->host),
  };
  if (!hub_request(hub, &depth, NULL, 0, deadline)) {
    return false;
  }
  return true;
}

static bool read_usb2_hub(struct usb_device_record *hub, unsigned *ports,
                          unsigned *power_delay, unsigned *tt, uint64_t deadline)
{
  struct usb_setup descriptor = {
    .request_type = USB_REQUEST_HUB_IN,
    .request = USB_REQUEST_GET_DESCRIPTOR,
    .value = USB_DESCRIPTOR_HUB << 8,
    .length = USB_HUB_PREFIX_BYTES,
  };
  uint8_t prefix[USB_HUB_PREFIX_BYTES];
  if (!hub_request(hub, &descriptor, prefix, sizeof(prefix), deadline)) {
    return false;
  }
  *ports = prefix[2];
  /* Keep the USB 2 variable bitmaps and unused compatibility padding separate
   * from the fixed USB 3 descriptor. */
  size_t minimum = USB_HUB_PREFIX_BYTES + (*ports + 1 + 7) / 8 + (*ports + 7) / 8;
  size_t total = prefix[0];
  unsigned characteristics = read16(prefix + 3);
  if (!*ports || prefix[1] != USB_DESCRIPTOR_HUB || total < minimum ||
      total > hub->owner->capacity || (characteristics & USB_HUB_CHARACTERISTICS_RESERVED)) {
    hub->detail = "invalid USB 2 hub descriptor";
    return false;
  }
  descriptor.length = total;
  if (!hub_request(hub, &descriptor, hub->owner->descriptors, total, deadline)) {
    return false;
  }
  if (memcmp(prefix, hub->owner->descriptors, sizeof(prefix))) {
    hub->detail = "changing USB 2 hub descriptor";
    return false;
  }
  *tt = hub->speed == USB_SPEED_HIGH ? (characteristics >> USB_HUB_TT_SHIFT) & USB_HUB_TT_MASK : 0;
  *power_delay = prefix[5] * 2;
  return true;
}

static bool prepare_hub(struct usb_device_record *hub, unsigned *ports,
                        uint64_t deadline)
{
  if (hub->incomplete || !hub->hub_configuration) {
    hub->detail = "hub shape or speed unsupported for traversal";
    return false;
  }
  struct usb_setup configure = {
    .request_type = USB_REQUEST_DEVICE_OUT,
    .request = USB_REQUEST_SET_CONFIGURATION,
    .value = hub->hub_configuration,
  };
  if (!hub_request(hub, &configure, NULL, 0, deadline)) {
    return false;
  }
  bool enhanced = usb_speed_is_enhanced(hub->speed);
  unsigned tt = 0, power_delay = 0;
  if (enhanced ? !read_usb3_hub(hub, ports, &power_delay, deadline) :
                 !read_usb2_hub(hub, ports, &power_delay, &tt, deadline)) {
    return false;
  }
  if (!request_ok(hub, usb_host_configure_hub(hub->host, *ports, tt, false, deadline))) {
    return false;
  }
  for (unsigned port = 1; port <= *ports; ++port) {
    if (!hub_feature(hub, port, USB_HUB_FEATURE_POWER, true, deadline)) {
      return false;
    }
  }
  if (power_delay < USB_PORT_POWER_DELAY_MS) {
    power_delay = USB_PORT_POWER_DELAY_MS;
  }
  /* The extra USB 2 attachment interval is also a conservative USB 3 boot
   * settling policy, rather than a claimed USB 3 timing requirement. */
  if (!hub_sleep(power_delay + USB_SIGNAL_ATTACH_MS, deadline)) {
    return false;
  }
  struct usb_setup status = {
    .request_type = USB_REQUEST_HUB_IN,
    .request = USB_REQUEST_GET_STATUS,
    .length = USB_HUB_STATUS_BYTES,
  };
  uint8_t bytes[USB_HUB_STATUS_BYTES];
  if (!hub_request(hub, &status, bytes, sizeof(bytes), deadline)) {
    return false;
  }
  if (read16(bytes) & USB_HUB_OVERCURRENT) {
    hub->detail = "hub reports overcurrent";
    return false;
  }
  return true;
}

static void inspect_hub(struct usb_device_record *hub, uint64_t deadline)
{
  unsigned ports;
  if (!prepare_hub(hub, &ports, deadline)) {
    hub->incomplete = true;
    return;
  }
  /* bNbrPorts is one byte. Capture candidates before resets, without accepting
   * later insertions as part of this hub's boot observation. */
  uint16_t initial_status[UINT8_MAX + 1];
  struct usb_discovery *discovery = hub->owner;
  size_t first = discovery->device_count;
  for (unsigned port = 1; port <= ports; ++port) {
    uint16_t change;
    if (!hub_port_status(hub, port, &initial_status[port], &change, deadline)) {
      goto capture_failed;
    }
    if (hub_port_failed(hub, initial_status[port], change)) {
      hub->incomplete = true;
      hub->detail = "USB 3 hub port link failed";
    }
    if (!(initial_status[port] & hub_power_bit(hub))) {
      hub->incomplete = true;
      hub->detail = "hub port power unavailable";
    }
    if (initial_status[port] & USB_HUB_PORT_CONNECTION) {
      if (discovery->device_count == discovery->device_capacity) {
        hub->incomplete = true;
        hub->detail = "hub reserved device budget exhausted";
      } else {
        struct usb_device_record *child = &discovery->devices[discovery->device_count++];
        child->owner = discovery;
        child->present = true;
        child->info.root_port = hub->info.root_port;
        child->info.parent_index = hub - discovery->devices;
        child->info.parent_port = port;
      }
    }
    /* Clear only the captured changes now. Delaying this until child setup
     * could erase a new connection change while earlier children are inspected. */
    if (!hub_acknowledge(hub, port, change, deadline)) {
      goto capture_failed;
    }
  }
  size_t last = discovery->device_count;
  for (size_t index = first; index < last; ++index) {
    struct usb_device_record *child = &discovery->devices[index];
    unsigned port = child->info.parent_port;
    if (discovery->hardware_failed || task_deadline_expired(deadline)) {
      child->incomplete = true;
      child->detail = "hub traversal stopped before child setup";
      hub->incomplete = true;
      continue;
    }
    struct usb_link link = { .speed = hub_reset_port(hub, port, deadline) };
    if (link.speed != USB_SPEED_UNKNOWN && !hub_child_link(hub, port, &link, deadline)) {
      link.speed = USB_SPEED_UNKNOWN;
    }
    child->speed = link.speed;
    child->info.speed = observation_speed(link.speed);
    if (link.speed != USB_SPEED_UNKNOWN &&
        !request_ok(child, usb_host_attach_child(hub->host, port, &link,
                                                deadline, &child->host))) {
      child->host = NULL;
    }
    inspect_record(child, deadline);
    if (child->incomplete) {
      hub->incomplete = true;
    }
  }
  if (discovery->hardware_failed) {
    return;
  }
  for (unsigned port = 1; port <= ports; ++port) {
    uint16_t status, change;
    if (!hub_port_status(hub, port, &status, &change, deadline)) {
      return;
    }
    if (((status ^ initial_status[port]) & USB_HUB_PORT_CONNECTION) ||
        (change & USB_HUB_CHANGE_CONNECTION) ||
        !(status & hub_power_bit(hub)) ||
        (status & USB_HUB_PORT_OVERCURRENT) || hub_port_failed(hub, status, change) ||
        (usb_speed_is_enhanced(hub->speed) && (status & USB_HUB_PORT_CONNECTION) &&
         (!(status & USB_HUB_PORT_ENABLE) || (status & USB_HUB_PORT_RESET) ||
          ((status >> USB_SUPER_LINK_SHIFT) & USB_SUPER_LINK_MASK)))) {
      hub->incomplete = true;
      hub->detail = "hub connection or power changed during boot traversal";
    }
  }
  return;

capture_failed:
  for (size_t index = first; index < discovery->device_count; ++index) {
    discovery->devices[index].incomplete = true;
    discovery->devices[index].detail = "hub snapshot failed before child setup";
  }
}

/* Capture every boot-present root port before requests can stop the controller. */
static void capture_ports(struct usb_discovery *discovery)
{
  for (unsigned port = 0; port < discovery->port_count; ++port) {
    if (!usb_host_port_present(discovery->host, port)) {
      continue;
    }
    struct usb_device_record *device = &discovery->devices[discovery->device_count++];
    device->owner = discovery;
    device->present = true;
    device->info.parent_index = SYSTEM_INFO_USB_NO_PARENT;
    device->host = usb_host_device_at(discovery->host, port);
    device->info.root_port = port + 1;
    if (device->host) {
      device->speed = usb_host_device_speed(device->host);
      device->info.speed = observation_speed(device->speed);
    }
  }
  discovery->device_capacity = discovery->device_count + usb_host_descendant_capacity(discovery->host);
}

void usb_inventory_controller_failed(size_t index)
{
  uint64_t flags = cpu_save_interrupts();
  if (index < inventory.controller_count && inventory.controllers[index].discovery) {
    usb_block_fail(inventory.controllers[index].discovery->storage_pool);
  }
  if (index >= inventory.controller_count || !inventory.controllers[index].pending) {
    cpu_restore_interrupts(flags);
    return;
  }
  struct usb_controller_record *controller = &inventory.controllers[index];
  struct usb_discovery *discovery = controller->discovery;
  if (discovery) {
    discovery->hardware_failed = true;
    if (discovery->started) {
      cpu_restore_interrupts(flags);
      return;
    }
    capture_ports(discovery);
    for (unsigned port = 0; port < discovery->device_count; ++port) {
      if (discovery->devices[port].present) {
        discovery->devices[port].info.flags |= SYSTEM_INFO_USB_DEVICE_INCOMPLETE;
      }
    }
  }
  controller->info.state = SYSTEM_INFO_USB_CONTROLLER_FAILED;
  controller->pending = false;
  inventory.incomplete = true;
  --inventory.pending;
  publish_inventory();
  cpu_restore_interrupts(flags);
  klog("usb: %x:%x.%u controller discovery failed\n", controller->info.pci.bus,
       controller->info.pci.device, controller->info.pci.function);
}

static void log_device(const struct usb_device_record *device)
{
  /* A path contains one root and at most the reserved descendant count.
   * Each uint16_t port needs at most five digits and a separator. */
  char path[(USB_DESCENDANT_BUDGET + 1) * sizeof("65535")];
  char *cursor = path + sizeof(path) - 1;
  *cursor = '\0';
  const struct usb_device_record *ancestor = device;
  for (;;) {
    bool root = ancestor->info.parent_index == SYSTEM_INFO_USB_NO_PARENT;
    unsigned port = root ? ancestor->info.root_port : ancestor->info.parent_port;
    do {
      *--cursor = '0' + port % 10;
      port /= 10;
    } while (port);
    if (root) {
      break;
    }
    *--cursor = '.';
    ancestor = &device->owner->devices[ancestor->info.parent_index];
  }
  const struct system_info_pci_function *pci =
    &inventory.controllers[device->owner->registry_index].info.pci;
  klog("usb: %x:%x.%u port %s device %x:%x%s%s%s\n", pci->bus, pci->device,
       pci->function, cursor, device->info.vendor_id, device->info.product_id,
       device->incomplete ? " (incomplete)" : "", device->detail ? ": " : "",
       device->detail ? device->detail : "");
  const struct usb_bot *storage = &device->storage;
  if (storage->state == USB_BOT_UNBOUND) {
    return;
  }
  klog("usb-bot: %x:%x.%u port %s: %s%s%s\n", pci->bus, pci->device,
       pci->function, cursor, storage->state == USB_BOT_READY ? "read-only probe ready" :
       storage->state == USB_BOT_UNSUPPORTED ? "unsupported" : "failed",
       storage->detail ? ": " : "", storage->detail ? storage->detail : "");
  if (storage->state == USB_BOT_READY) {
    klog("usb-bot: %x:%x.%u port %s: blocks=%llu block-bytes=%u read-bytes=%llu\n",
         pci->bus, pci->device, pci->function, cursor, (unsigned long long)storage->blocks,
         storage->block_bytes, (unsigned long long)storage->read_bytes);
  }
}

void usb_enumerate(struct usb_discovery *discovery, uint64_t deadline)
{
  if (!discovery || discovery->started ||
      !inventory.controllers[discovery->registry_index].pending) {
    return;
  }
  discovery->started = true;
  capture_ports(discovery);
  bool incomplete = !usb_host_inventory_complete(discovery->host);
  for (unsigned port = 0; port < discovery->device_count; ++port) {
    struct usb_device_record *device = &discovery->devices[port];
    if (!device->present) {
      continue;
    }
    inspect_record(device, deadline);
    if (device->incomplete) {
      device->info.flags |= SYSTEM_INFO_USB_DEVICE_INCOMPLETE;
      incomplete = true;
    }
  }
  /* Roots are inspected first. Appended children make this an iterative
   * breadth-first walk, with every parent preceding its descendants. */
  for (size_t index = 0; index < discovery->device_count; ++index) {
    struct usb_device_record *device = &discovery->devices[index];
    if (device->info.flags & SYSTEM_INFO_USB_DEVICE_HUB) {
      inspect_hub(device, deadline);
    }
    if (device->incomplete) {
      device->info.flags |= SYSTEM_INFO_USB_DEVICE_INCOMPLETE;
      incomplete = true;
    }
  }
  for (size_t index = 0; index < discovery->device_count; ++index) {
    struct usb_device_record *device = &discovery->devices[index];
    struct usb_bot *storage = &device->storage;
    if (storage->configuration) {
      if (device->incomplete || discovery->hardware_failed || task_deadline_expired(deadline)) {
        storage->state = USB_BOT_FAILED;
        storage->detail = "storage probe skipped after incomplete inspection or controller setup";
      } else {
        usb_bot_probe(storage, discovery->storage_scratch, USB_BULK_BYTES, deadline);
      }
    }
    log_device(device);
    if (storage->state != USB_BOT_UNBOUND) {
      uint64_t flags = cpu_save_interrupts();
      block_register_usb(usb_block_bind(discovery->storage_pool, storage));
      cpu_restore_interrupts(flags);
    }
  }
  incomplete |= !usb_host_inventory_complete(discovery->host) || task_deadline_expired(deadline);
  uint64_t flags = cpu_save_interrupts();
  struct usb_controller_record *controller = &inventory.controllers[discovery->registry_index];
  controller->info.state = discovery->hardware_failed ? SYSTEM_INFO_USB_CONTROLLER_FAILED :
    incomplete ? SYSTEM_INFO_USB_CONTROLLER_INCOMPLETE : SYSTEM_INFO_USB_CONTROLLER_COMPLETE;
  controller->pending = false;
  inventory.incomplete |= incomplete || discovery->hardware_failed;
  --inventory.pending;
  publish_inventory();
  cpu_restore_interrupts(flags);
}

size_t usb_storage_capacity(void)
{
  size_t capacity = 0;
  for (size_t index = 0; index < inventory.controller_count; ++index) {
    const struct usb_discovery *discovery = inventory.controllers[index].discovery;
    if (discovery) {
      if (discovery->device_capacity > SIZE_MAX - capacity) {
        return SIZE_MAX;
      }
      capacity += discovery->device_capacity;
    }
  }
  return capacity;
}

void usb_storage_process(struct usb_discovery *discovery)
{
  if (discovery) {
    usb_block_process(discovery->storage_pool);
  }
}

static bool inventory_published(void)
{
  uint64_t state = atomic_load_explicit(&inventory.state, memory_order_acquire);
  return state == SYSTEM_INFO_USB_COMPLETE || state == SYSTEM_INFO_USB_INCOMPLETE;
}

void usb_inventory_read(struct system_info_usb *reply)
{
  uint64_t state = atomic_load_explicit(&inventory.state, memory_order_acquire);
  if (state == SYSTEM_INFO_USB_COMPLETE || state == SYSTEM_INFO_USB_INCOMPLETE) {
    *reply = inventory.info;
  } else {
    *reply = (struct system_info_usb) { .state = state };
  }
}

bool usb_inventory_read_controller(uint64_t index, struct system_info_usb_controller *reply)
{
  if (!inventory_published() || index >= inventory.info.controller_count) {
    return false;
  }
  *reply = inventory.controllers[index].info;
  return true;
}

bool usb_inventory_read_device(uint64_t index, struct system_info_usb_device *reply)
{
  if (!inventory_published() || index >= inventory.info.device_count) {
    return false;
  }
  for (size_t i = 0; i < inventory.controller_count; ++i) {
    const struct usb_controller_record *controller = &inventory.controllers[i];
    const struct usb_discovery *discovery = controller->discovery;
    if (!discovery || index < controller->device_first ||
        index - controller->device_first >= discovery->device_count) {
      continue;
    }
    size_t local = index - controller->device_first;
    for (unsigned port = 0; port < discovery->device_count; ++port) {
      if (discovery->devices[port].present) {
        if (!local) {
          *reply = discovery->devices[port].info;
          return true;
        }
        --local;
      }
    }
  }
  return false;
}

bool usb_inventory_read_interface(uint64_t index, struct system_info_usb_interface *reply)
{
  if (!inventory_published() || index >= inventory.info.interface_count) {
    return false;
  }
  for (size_t i = 0; i < inventory.controller_count; ++i) {
    const struct usb_controller_record *controller = &inventory.controllers[i];
    const struct usb_discovery *discovery = controller->discovery;
    if (discovery && index >= controller->interface_first &&
        index - controller->interface_first < discovery->interface_count) {
      *reply = discovery->interfaces[index - controller->interface_first];
      return true;
    }
  }
  return false;
}
