#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/pci.h>
#include <kernel/task.h>
#include <limits.h>
#include <stdatomic.h>
#include "core.h"
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
  struct system_info_usb_device info;
  enum usb_speed speed;
  const char *detail;
  bool present, incomplete;
};

struct usb_discovery {
  struct usb_host_controller *host;
  struct usb_device_record *devices;
  struct system_info_usb_interface *interfaces;
  uint8_t *descriptors;
  size_t capacity, interface_count, device_count, registry_index;
  unsigned port_count;
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
    device->incomplete = true;
    device->detail = "hub downstream inventory unavailable";
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
      companion_pending = device->speed == USB_SPEED_SUPER ? USB_DESCRIPTOR_SUPER_COMPANION : 0;
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
    for (unsigned port = 0; port < discovery->port_count; ++port) {
      struct usb_device_record *device = &discovery->devices[port];
      if (!device->present) {
        continue;
      }
      device->info.controller_index = index;
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
  size_t capacity = usb_host_control_capacity();
  controller->info.root_port_count = ports;
  if (!ports || ports > UINT16_MAX || sizeof(struct usb_device_record) > SIZE_MAX / ports ||
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
  discovery->capacity = capacity;
  discovery->devices = kmalloc(ports * sizeof(*discovery->devices));
  discovery->descriptors = kmalloc(capacity);
  discovery->interfaces = kmalloc(USB_INTERFACE_BUDGET * sizeof(*discovery->interfaces));
  if (!discovery->devices || !discovery->descriptors || !discovery->interfaces) {
    usb_release_prepared(discovery);
    return NULL;
  }
  memset(discovery->devices, 0, ports * sizeof(*discovery->devices));
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
  default: return SYSTEM_INFO_USB_SPEED_UNKNOWN;
  }
}

/* Capture every boot-present root port before requests can stop the controller. */
static void capture_ports(struct usb_discovery *discovery)
{
  for (unsigned port = 0; port < discovery->port_count; ++port) {
    struct usb_device_record *device = &discovery->devices[port];
    device->owner = discovery;
    device->present = usb_host_port_present(discovery->host, port);
    if (!device->present) {
      continue;
    }
    ++discovery->device_count;
    device->host = usb_host_device_at(discovery->host, port);
    device->info.root_port = port + 1;
    if (device->host) {
      device->speed = usb_host_device_speed(device->host);
      device->info.speed = observation_speed(device->speed);
    }
  }
}

void usb_inventory_controller_failed(size_t index)
{
  uint64_t flags = cpu_save_interrupts();
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
    for (unsigned port = 0; port < discovery->port_count; ++port) {
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

void usb_enumerate(struct usb_discovery *discovery, uint64_t deadline)
{
  if (!discovery || discovery->started ||
      !inventory.controllers[discovery->registry_index].pending) {
    return;
  }
  discovery->started = true;
  capture_ports(discovery);
  bool incomplete = !usb_host_inventory_complete(discovery->host);
  for (unsigned port = 0; port < discovery->port_count; ++port) {
    struct usb_device_record *device = &discovery->devices[port];
    if (!device->present) {
      continue;
    }
    device->info.interface_first = discovery->interface_count;
    if (!device->host || discovery->hardware_failed || task_deadline_expired(deadline)) {
      device->incomplete = true;
      device->detail = "connected port could not be inspected";
    } else {
      inspect_device(device, deadline);
    }
    device->info.interface_count = discovery->interface_count - device->info.interface_first;
    if (device->incomplete) {
      device->info.flags |= SYSTEM_INFO_USB_DEVICE_INCOMPLETE;
      incomplete = true;
    }
    const struct system_info_pci_function *pci =
      &inventory.controllers[discovery->registry_index].info.pci;
    klog("usb: %x:%x.%u port %u device %x:%x%s%s\n", pci->bus, pci->device, pci->function,
         port + 1, device->info.vendor_id, device->info.product_id,
         device->detail ? ": " : "", device->detail ? device->detail : "");
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
    for (unsigned port = 0; port < discovery->port_count; ++port) {
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
