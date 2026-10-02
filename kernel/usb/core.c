#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/task.h>
#include <limits.h>
#include "core.h"
#include "bot.h"
#include "settings.h"

#define USB_REQUEST_GET_DESCRIPTOR 6
#define USB_REQUEST_SET_CONFIGURATION 9
#define USB_REQUEST_SET_INTERFACE 11
#define USB_REQUEST_DEVICE_IN 0x80
#define USB_REQUEST_INTERFACE_OUT 0x01
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
#define USB_CLASS_INTERFACE 0x00
#define USB_CLASS_STORAGE 0x08
#define USB_CLASS_HUB 0x09
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

enum device_classification { DEVICE_UNKNOWN, DEVICE_UNBOUND, DEVICE_STORAGE_UNSUPPORTED,
                             DEVICE_BOT_CANDIDATE, DEVICE_INCOMPLETE };
enum discovery_result { DISCOVERY_NOT_STARTED, DISCOVERY_INCOMPLETE, DISCOVERY_ABSENT,
                        DISCOVERY_UNSUPPORTED, DISCOVERY_AMBIGUOUS, DISCOVERY_SETUP_FAILED,
                        DISCOVERY_TRANSPORT_READY };

struct usb_device_record {
  struct usb_host_device *host;
  enum usb_speed speed;
  enum device_classification classification;
  const char *detail;
  uint16_t vendor, product;
  uint8_t device_class, configuration_count;
  bool storage, incomplete, candidate;
  struct usb_bot_candidate bot;
};

/* This retained state is inspectable in GDB. It is worker-owned after the
 * pre-AP allocation phase, and does not publish a registry or media backend. */
static struct {
  struct usb_device_record *devices;
  uint8_t *descriptors;
  size_t capacity;
  unsigned port_count, candidates, selected_port;
  enum discovery_result result;
  bool prepared, published, hardware_failed;
} discovery;

static uint16_t read16(const uint8_t *bytes)
{
  return bytes[0] | ((uint16_t)bytes[1] << 8);
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
    discovery.hardware_failed = true;
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
  if (!request_ok(device, control(device, &setup, discovery.descriptors,
                                 discovery.capacity, &bytes, deadline))) {
    return false;
  }
  if (bytes != length) {
    device->incomplete = true;
    device->detail = "short descriptor";
    return false;
  }
  return true;
}

static bool known_class(uint8_t class)
{
  switch (class) {
  case 0x01: /* Audio */
  case 0x02: /* Communications */
  case 0x03: /* HID */
  case 0x05: /* Physical */
  case 0x06: /* Imaging */
  case 0x07: /* Printer */
  case USB_CLASS_STORAGE:
  case USB_CLASS_HUB:
  case 0x0a: /* Communications data */
  case 0x0b: /* Smart card */
  case 0x0d: /* Content security */
  case 0x0e: /* Video */
  case 0x0f: /* Personal healthcare */
  case 0x10: /* Audio/video */
  case 0x11: /* Billboard */
  case 0x12: /* Type-C bridge */
  case 0xe0: /* Wireless controller */
  case 0xef: /* Miscellaneous */
  case 0xfe: /* Application specific */
    return true;
  default:
    return false;
  }
}

static void classify_class(struct usb_device_record *device, uint8_t class)
{
  if (class == USB_CLASS_STORAGE) {
    device->storage = true;
  } else if (class == USB_CLASS_HUB) {
    device->incomplete = true;
    device->detail = "hub downstream inventory unavailable";
  } else if (!known_class(class)) {
    device->incomplete = true;
    device->detail = "unclassifiable USB class";
  }
}

/* The buffer is bounded, so rescanning earlier checked descriptors avoids a
 * 256-by-256 identity table on the 16 KiB kernel stack. */
static bool duplicate_interface(size_t before, uint8_t number, uint8_t alternate)
{
  for (size_t offset = USB_CONFIGURATION_BYTES; offset < before;
       offset += discovery.descriptors[offset]) {
    const uint8_t *descriptor = discovery.descriptors + offset;
    if (descriptor[1] == USB_DESCRIPTOR_INTERFACE &&
        descriptor[2] == number && descriptor[3] == alternate) {
      return true;
    }
  }
  return false;
}

static bool duplicate_endpoint(size_t before, uint8_t number, uint8_t alternate,
                                uint8_t address)
{
  uint8_t previous_number = 0, previous_alternate = 0;
  for (size_t offset = USB_CONFIGURATION_BYTES; offset < before;
       offset += discovery.descriptors[offset]) {
    const uint8_t *descriptor = discovery.descriptors + offset;
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

static void finish_interface(struct usb_device_record *device, uint8_t interfaces,
                              uint8_t configuration,
                              const struct usb_interface_description *interface)
{
  struct usb_bot_candidate candidate;
  if (!device->candidate && usb_bot_match(device->speed, device->device_class,
                                         interfaces, configuration, interface, &candidate)) {
    device->candidate = true;
    device->bot = candidate;
  }
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
        (speed == USB_SPEED_SUPER ? usage > 1 : usage != 0)) {
      return false;
    }
    if (!endpoint[6] || ((speed == USB_SPEED_SUPER || speed == USB_SPEED_HIGH) && endpoint[6] > 16) ||
        (speed == USB_SPEED_SUPER && usage == 1 && endpoint[6] < 8)) {
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
  const uint8_t *header = discovery.descriptors;
  uint8_t interfaces = header[4], configuration = header[5];
  uint8_t numbers[USB_BITSET_BYTES] = {0}, default_alternates[USB_BITSET_BYTES] = {0};
  struct usb_interface_description interface = {0};
  unsigned unique_interfaces = 0, endpoints = 0;
  bool have_interface = false;
  uint8_t companion_pending = 0;
  const uint8_t *last_endpoint = NULL;

  for (size_t offset = USB_CONFIGURATION_BYTES; offset < total;) {
    if (total - offset < 2) {
      return false;
    }
    const uint8_t *part = discovery.descriptors + offset;
    size_t length = part[0];
    if (length < 2 || length > total - offset ||
        (companion_pending && part[1] != companion_pending)) {
      return false;
    }
    switch (part[1]) {
    case USB_DESCRIPTOR_INTERFACE:
      if (length != USB_INTERFACE_BYTES || part[2] >= interfaces ||
          duplicate_interface(offset, part[2], part[3])) {
        return false;
      }
      if (have_interface) {
        if (endpoints != interface.endpoint_count) {
          return false;
        }
        finish_interface(device, interfaces, configuration, &interface);
      }
      memset(&interface, 0, sizeof(interface));
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
      classify_class(device, interface.class);
      endpoints = 0;
      have_interface = true;
      break;
    case USB_DESCRIPTOR_ENDPOINT: {
      if (!have_interface || length < USB_ENDPOINT_BYTES ||
          endpoints >= interface.endpoint_count || !(part[2] & USB_ENDPOINT_NUMBER_MASK) ||
          (part[2] & USB_ENDPOINT_ADDRESS_RESERVED) ||
          duplicate_endpoint(offset, interface.number, interface.alternate, part[2]) ||
          !valid_endpoint(device->speed, part)) {
        return false;
      }
      uint16_t packet = read16(part + 4);
      if (endpoints < 2) {
        interface.endpoints[endpoints].attributes = part[3];
        interface.endpoints[endpoints].endpoint.address = part[2];
        interface.endpoints[endpoints].endpoint.max_packet = packet & USB_PACKET_SIZE_MASK;
      }
      ++endpoints;
      last_endpoint = part;
      companion_pending = device->speed == USB_SPEED_SUPER ? USB_DESCRIPTOR_SUPER_COMPANION : 0;
      break;
    }
    case USB_DESCRIPTOR_SUPER_COMPANION:
      if (companion_pending != USB_DESCRIPTOR_SUPER_COMPANION || length != USB_SUPER_COMPANION_BYTES ||
          !valid_companion(last_endpoint, part)) {
        return false;
      }
      if (endpoints <= 2) {
        interface.endpoints[endpoints - 1].companion = true;
        interface.endpoints[endpoints - 1].companion_attributes = part[3];
        interface.endpoints[endpoints - 1].bytes_per_interval = read16(part + 4);
        interface.endpoints[endpoints - 1].endpoint.max_burst = part[2];
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
  finish_interface(device, interfaces, configuration, &interface);
  return true;
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
    *packet = 512;
    return wire == 9;
  default:
    return false;
  }
}

static void inspect_device(struct usb_device_record *device, uint64_t deadline)
{
  device->speed = usb_host_device_speed(device->host);
  if (!request_ok(device, usb_host_address(device->host, deadline)) ||
      !descriptor(device, USB_DESCRIPTOR_DEVICE, 0, USB_DEVICE_PREFIX_BYTES, deadline)) {
    return;
  }
  uint8_t prefix[USB_DEVICE_PREFIX_BYTES];
  memcpy(prefix, discovery.descriptors, sizeof(prefix));
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
  const uint8_t *description = discovery.descriptors;
  if (memcmp(prefix, description, sizeof(prefix)) || !description[17]) {
    device->incomplete = true;
    device->detail = "invalid or changing device descriptor";
    return;
  }
  device->vendor = read16(description + 8);
  device->product = read16(description + 10);
  device->device_class = description[4];
  device->configuration_count = description[17];
  if (device->device_class != USB_CLASS_INTERFACE) {
    classify_class(device, device->device_class);
  }

  uint8_t configurations[USB_BITSET_BYTES] = {0};
  for (unsigned index = 0; index < device->configuration_count; ++index) {
    if (!descriptor(device, USB_DESCRIPTOR_CONFIGURATION, index, USB_CONFIGURATION_BYTES, deadline)) {
      if (discovery.hardware_failed) {
        return;
      }
      continue;
    }
    uint8_t header[USB_CONFIGURATION_BYTES];
    memcpy(header, discovery.descriptors, sizeof(header));
    size_t total = read16(header + 2);
    if (header[0] != USB_CONFIGURATION_BYTES || header[1] != USB_DESCRIPTOR_CONFIGURATION ||
        total < USB_CONFIGURATION_BYTES || !header[4] || !header[5] ||
        !(header[7] & USB_CONFIGURATION_REQUIRED) || (header[7] & USB_CONFIGURATION_RESERVED) ||
        bit_set(configurations, header[5])) {
      device->incomplete = true;
      device->detail = "invalid or duplicate configuration header";
      continue;
    }
    if (total > discovery.capacity) {
      device->incomplete = true;
      device->detail = "configuration exceeds reserved descriptor budget";
      continue;
    }
    if (!descriptor(device, USB_DESCRIPTOR_CONFIGURATION, index, total, deadline)) {
      if (discovery.hardware_failed) {
        return;
      }
      continue;
    }
    bool previous_candidate = device->candidate;
    struct usb_bot_candidate previous_bot = device->bot;
    if (memcmp(header, discovery.descriptors, sizeof(header)) || !parse_configuration(device, total)) {
      device->candidate = previous_candidate;
      device->bot = previous_bot;
      device->incomplete = true;
      device->detail = "malformed or changing configuration descriptors";
    }
  }
}

static const char *classification_name(enum device_classification classification)
{
  switch (classification) {
  case DEVICE_UNBOUND: return "unbound";
  case DEVICE_STORAGE_UNSUPPORTED: return "unsupported storage";
  case DEVICE_BOT_CANDIDATE: return "provisional SCSI/BOT candidate";
  case DEVICE_INCOMPLETE: return "incomplete";
  default: return "unknown";
  }
}

bool usb_prepare(void)
{
  if (discovery.prepared || discovery.published) {
    return false;
  }
  discovery.port_count = usb_host_port_count();
  discovery.capacity = usb_host_control_capacity();
  if (!discovery.port_count || sizeof(*discovery.devices) > SIZE_MAX / discovery.port_count ||
      discovery.capacity < USB_DEVICE_BYTES || discovery.capacity > UINT16_MAX) {
    return false;
  }
  discovery.devices = kmalloc(discovery.port_count * sizeof(*discovery.devices));
  discovery.descriptors = kmalloc(discovery.capacity);
  if (!discovery.devices || !discovery.descriptors) {
    usb_release_prepared();
    return false;
  }
  memset(discovery.devices, 0, discovery.port_count * sizeof(*discovery.devices));
  discovery.prepared = true;
  return true;
}

void usb_release_prepared(void)
{
  if (discovery.published) {
    return;
  }
  kfree(discovery.descriptors);
  kfree(discovery.devices);
  memset(&discovery, 0, sizeof(discovery));
}

static bool configure_candidate(struct usb_device_record *device, uint64_t deadline)
{
  struct usb_setup setup = {
    .request = USB_REQUEST_SET_CONFIGURATION,
    .value = device->bot.configuration,
  };
  size_t bytes;
  if (!request_ok(device, control(device, &setup, NULL, 0, &bytes, deadline)) || bytes) {
    return false;
  }
  if (device->bot.alternate) {
    setup = (struct usb_setup) {
      .request_type = USB_REQUEST_INTERFACE_OUT,
      .request = USB_REQUEST_SET_INTERFACE,
      .value = device->bot.alternate,
      .index = device->bot.interface,
    };
    if (!request_ok(device, control(device, &setup, NULL, 0, &bytes, deadline)) || bytes) {
      return false;
    }
  }
  return request_ok(device, usb_host_configure_bulk(device->host, device->bot.configuration,
                    device->bot.interface, device->bot.alternate, device->bot.endpoints, 2, deadline));
}

void usb_enumerate(uint64_t deadline)
{
  if (!discovery.prepared || discovery.published) {
    return;
  }
  discovery.published = true;
  discovery.result = DISCOVERY_INCOMPLETE;
  discovery.selected_port = UINT_MAX;
  bool incomplete = !usb_host_inventory_complete(), unsupported_storage = false;

  for (unsigned index = 0; index < discovery.port_count; ++index) {
    struct usb_device_record *device = &discovery.devices[index];
    device->host = usb_host_device_at(index);
    if (!device->host) {
      continue;
    }
    inspect_device(device, deadline);
    if (device->incomplete) {
      device->classification = DEVICE_INCOMPLETE;
      incomplete = true;
    } else if (device->candidate) {
      device->classification = DEVICE_BOT_CANDIDATE;
      ++discovery.candidates;
      discovery.selected_port = index;
    } else if (device->storage) {
      device->classification = DEVICE_STORAGE_UNSUPPORTED;
      unsupported_storage = true;
    } else {
      device->classification = DEVICE_UNBOUND;
    }
    klog("usb: port %u device %04x:%04x %s%s%s\n", index + 1,
         device->vendor, device->product, classification_name(device->classification),
         device->detail ? ": " : "", device->detail ? device->detail : "");
    if (discovery.hardware_failed) {
      incomplete = true;
      break;
    }
  }
  incomplete |= !usb_host_inventory_complete() || task_deadline_expired(deadline);
  if (incomplete) {
    discovery.selected_port = UINT_MAX;
    klog("usb: discovery incomplete; no transport selected\n");
    return;
  }
  if (discovery.candidates > 1) {
    discovery.result = DISCOVERY_AMBIGUOUS;
    discovery.selected_port = UINT_MAX;
    klog("usb: discovery ambiguous (%u physical-device candidates)\n", discovery.candidates);
    return;
  }
  if (!discovery.candidates) {
    discovery.result = unsupported_storage ? DISCOVERY_UNSUPPORTED : DISCOVERY_ABSENT;
    klog("usb: discovery %s\n", unsupported_storage ? "unsupported storage" : "storage absent");
    return;
  }
  struct usb_device_record *device = &discovery.devices[discovery.selected_port];
  klog("usb: selected port %u device %04x:%04x configuration %u interface %u alternate %u\n",
       discovery.selected_port + 1, device->vendor, device->product,
       device->bot.configuration, device->bot.interface, device->bot.alternate);
  for (unsigned i = 0; i < 2; ++i) {
    klog("usb: bulk endpoint %02x packet %u burst %u\n", device->bot.endpoints[i].address,
         device->bot.endpoints[i].max_packet, device->bot.endpoints[i].max_burst);
  }
  if (!configure_candidate(device, deadline)) {
    discovery.result = DISCOVERY_SETUP_FAILED;
    klog("usb: selected transport setup failed; selection retained\n");
    return;
  }
  discovery.result = DISCOVERY_TRANSPORT_READY;
  klog("usb: provisional SCSI/BOT transport ready; media and LUN setup pending\n");
}
