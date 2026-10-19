#include <kernel/memory.h>
#include <kernel/net/driver.h>
#include <kernel/net/registry.h>
#include <kernel/net/controller.h>
#include <kernel/net/debug.h>
#include <kernel/net/panic_tx.h>
#include <kernel/net/ethernet.h>
#include <kernel/virtio/net.h>
#include <kernel/net/rtl8111.h>
#include <kernel/net/usb_ncm.h>
#include <kernel/net/log_udp.h>
#include <stdatomic.h>

/* Publish the immutable management selection before activation. Fatal entry
 * can close its driver gate even when activation is interrupted. */
static _Atomic(const struct net_interface *) panic_interface;
static atomic_bool handoff_enabled;
static uint32_t handoff_cpu;
static const struct net_interface *debug_interface;

void net_panic_context_enable(void)
{
  handoff_cpu = cpu_initial_apic_id();
  atomic_store_explicit(&handoff_enabled, true, memory_order_release);
}

bool net_panic_context_enabled(void)
{
  return atomic_load_explicit(&handoff_enabled, memory_order_acquire);
}

uint32_t net_panic_context_cpu(void)
{
  return handoff_cpu;
}

void net_driver_debug_enable(const struct net_interface *iface)
{
  debug_interface = iface;
  net_panic_context_enable();
}

static bool same_controller(struct net_driver_binding a, struct net_driver_binding b)
{
  return a.kind == b.kind && (a.kind == NET_DRIVER_KIND_VIRTIO ? a.virtio == b.virtio :
    a.kind == NET_DRIVER_KIND_RTL8111 ? a.rtl == b.rtl :
    a.kind == NET_DRIVER_KIND_USB_NCM ? a.ncm == b.ncm : true);
}

static enum call_status find_controller(const struct net_selector *selector,
    struct net_driver_binding *controller)
{
  for (size_t i = 0; i < sizeof(selector->reserved); ++i) {
    if (selector->reserved[i]) {
      return CALL_BAD_REQUEST;
    }
  }
  if (selector->kind == NET_SELECT_VIRTIO ||
      selector->kind == NET_SELECT_LINKED_CONTROLLER) {
    for (size_t i = 0; i < sizeof(selector->mac); ++i) {
      if (selector->mac[i]) {
        return CALL_BAD_REQUEST;
      }
    }
  } else if (selector->kind != NET_SELECT_MAC ||
             !net_ethernet_is_unicast(selector->mac)) {
    return CALL_BAD_REQUEST;
  }
  if ((selector->kind == NET_SELECT_LINKED_CONTROLLER) !=
      (selector->controller_id != 0)) {
    return CALL_BAD_REQUEST;
  }
  if (!virtio_net_inventory_complete() ||
      (selector->kind != NET_SELECT_VIRTIO &&
       (!rtl8111_inventory_complete() || !usb_ncm_inventory_complete()))) {
    return CALL_UNAVAILABLE;
  }

  struct net_driver_binding match = {0};
  bool unknown_identity = false;
  for (struct virtio_net_controller *candidate = virtio_net_first(); candidate;
       candidate = virtio_net_next(candidate)) {
    if (selector->kind == NET_SELECT_LINKED_CONTROLLER &&
        virtio_net_controller_id(candidate) != selector->controller_id) {
      continue;
    }
    if (selector->kind == NET_SELECT_MAC) {
      const uint8_t *mac = virtio_net_identity_mac(candidate);
      if (!mac) {
        unknown_identity = true;
        continue;
      }
      if (memcmp(mac, selector->mac, sizeof(selector->mac))) {
        continue;
      }
    }
    if (match.kind != NET_DRIVER_KIND_NONE) {
      return CALL_BUSY;
    }
    match = (struct net_driver_binding){.kind = NET_DRIVER_KIND_VIRTIO, .virtio = candidate};
  }
  if (selector->kind != NET_SELECT_VIRTIO) {
    for (struct rtl8111_controller *candidate = rtl8111_first(); candidate;
         candidate = rtl8111_next(candidate)) {
      if (selector->kind == NET_SELECT_LINKED_CONTROLLER) {
        if (rtl8111_controller_id(candidate) != selector->controller_id) {
          continue;
        }
      } else {
        const uint8_t *mac = rtl8111_identity_mac(candidate);
        if (!mac) {
          unknown_identity = true;
          continue;
        }
        if (memcmp(mac, selector->mac, sizeof(selector->mac))) {
          continue;
        }
      }
      if (match.kind != NET_DRIVER_KIND_NONE) {
        return CALL_BUSY;
      }
      match = (struct net_driver_binding){.kind = NET_DRIVER_KIND_RTL8111, .rtl = candidate};
    }
    for (struct usb_ncm_nic *candidate = usb_ncm_first(); candidate;
         candidate = usb_ncm_next(candidate)) {
      if (selector->kind == NET_SELECT_LINKED_CONTROLLER) {
        if (usb_ncm_controller_id(candidate) != selector->controller_id) {
          continue;
        }
      } else {
        const uint8_t *mac = usb_ncm_identity_mac(candidate);
        if (memcmp(mac, selector->mac, sizeof(selector->mac))) {
          continue;
        }
      }
      if (match.kind != NET_DRIVER_KIND_NONE) {
        return CALL_BUSY;
      }
      match = (struct net_driver_binding){.kind = NET_DRIVER_KIND_USB_NCM, .ncm = candidate};
    }
  }
  if (unknown_identity) {
    return CALL_UNAVAILABLE;
  }
  if (match.kind == NET_DRIVER_KIND_NONE) {
    return CALL_NOT_FOUND;
  }
  *controller = match;
  return CALL_OK;
}

enum call_status net_driver_resolve(const struct net_interface *iface,
    const struct net_selector *selector, struct net_driver_binding *controller)
{
  net_worker_assert_context();
  if (iface->interface_class != NET_INTERFACE_CLASS_ETHERNET) {
    return CALL_BAD_REQUEST;
  }
  enum call_status status = find_controller(selector, controller);
  if (status != CALL_OK) {
    return status;
  }
  if (iface->driver.kind != NET_DRIVER_KIND_NONE &&
      !same_controller(iface->driver, *controller)) {
    return CALL_BUSY;
  }
  for (struct net_interface *other = net_interface_first(); other;
      other = other->registry_next) {
    if (other != iface && same_controller(other->driver, *controller)) {
      return CALL_BUSY;
    }
  }
  if (iface->driver.kind == NET_DRIVER_KIND_NONE &&
      selector->kind == NET_SELECT_LINKED_CONTROLLER) {
    bool up;
    bool known = controller->kind == NET_DRIVER_KIND_VIRTIO ?
      virtio_net_carrier(controller->virtio, &up) :
      controller->kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_carrier(controller->rtl, &up) :
      usb_ncm_carrier(controller->ncm, &up);
    bool prepared = controller->kind == NET_DRIVER_KIND_VIRTIO ?
      virtio_net_prepared(controller->virtio) :
      controller->kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_prepared(controller->rtl) :
      usb_ncm_prepared(controller->ncm);
    if (!prepared || !known || !up) {
      return CALL_UNAVAILABLE;
    }
  }
  return net_log_udp_panicking() ? CALL_UNAVAILABLE : CALL_OK;
}

bool net_driver_same_controller(struct net_driver_binding a, struct net_driver_binding b)
{
  return same_controller(a, b);
}

void net_driver_activate(const struct net_interface *iface)
{
  net_worker_assert_context();
  if (iface->driver.kind == NET_DRIVER_KIND_VIRTIO) {
    virtio_net_start(iface->driver.virtio);
  } else if (iface->driver.kind == NET_DRIVER_KIND_RTL8111) {
    rtl8111_start(iface->driver.rtl);
  } else if (iface->driver.kind == NET_DRIVER_KIND_USB_NCM) {
    usb_ncm_start(iface->driver.ncm);
  }
}

void net_driver_panic_select(const struct net_interface *iface)
{
  atomic_store_explicit(&panic_interface, iface, memory_order_release);
}

const struct net_interface *net_driver_panic_selected(void)
{
  return atomic_load_explicit(&panic_interface, memory_order_acquire);
}

bool net_driver_panic_begin(const struct net_interface *iface, uint8_t mac[6])
{
  enum net_driver_kind kind = iface ? iface->driver.kind : NET_DRIVER_KIND_NONE;
  if (kind == NET_DRIVER_KIND_NONE || kind == NET_DRIVER_KIND_USB_NCM) {
    return false;
  }
  const uint8_t *identity = kind == NET_DRIVER_KIND_VIRTIO ?
      virtio_net_identity_mac(iface->driver.virtio) : rtl8111_identity_mac(iface->driver.rtl);
  bool ready = kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_panic_begin(iface->driver.virtio) :
      rtl8111_panic_begin(iface->driver.rtl);
  if (!ready || !identity) {
    return false;
  }
  memcpy(mac, identity, 6);
  return true;
}

bool net_driver_panic_transmit(const struct net_interface *iface,
    const void *frame, size_t length)
{
  enum net_driver_kind kind = iface ? iface->driver.kind : NET_DRIVER_KIND_NONE;
  return kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_panic_transmit(iface->driver.virtio, frame, length) :
      kind == NET_DRIVER_KIND_RTL8111 && rtl8111_panic_transmit(iface->driver.rtl, frame, length);
}

void net_driver_next_controller(uint32_t after_id,
    struct net_controller_reply *reply)
{
  net_worker_assert_context();
  *reply = (struct net_controller_reply){0};
  if (virtio_net_inventory_complete() && rtl8111_inventory_complete() &&
      usb_ncm_inventory_complete()) {
    reply->flags = NET_CONTROLLER_INVENTORY_COMPLETE;
  }
  struct net_driver_binding selected = {0};
  for (struct virtio_net_controller *candidate = virtio_net_first(); candidate;
       candidate = virtio_net_next(candidate)) {
    uint32_t id = virtio_net_controller_id(candidate);
    if (id > after_id && (!reply->controller_id || id < reply->controller_id)) {
      reply->controller_id = id;
      selected = (struct net_driver_binding){.kind = NET_DRIVER_KIND_VIRTIO, .virtio = candidate};
    }
  }
  for (struct rtl8111_controller *candidate = rtl8111_first(); candidate;
       candidate = rtl8111_next(candidate)) {
    uint32_t id = rtl8111_controller_id(candidate);
    if (id > after_id && (!reply->controller_id || id < reply->controller_id)) {
      reply->controller_id = id;
      selected = (struct net_driver_binding){.kind = NET_DRIVER_KIND_RTL8111, .rtl = candidate};
    }
  }
  for (struct usb_ncm_nic *candidate = usb_ncm_first(); candidate;
       candidate = usb_ncm_next(candidate)) {
    uint32_t id = usb_ncm_controller_id(candidate);
    if (id > after_id && (!reply->controller_id || id < reply->controller_id)) {
      reply->controller_id = id;
      selected = (struct net_driver_binding){.kind = NET_DRIVER_KIND_USB_NCM, .ncm = candidate};
    }
  }
  if (selected.kind == NET_DRIVER_KIND_NONE) {
    return;
  }
  bool virtio = selected.kind == NET_DRIVER_KIND_VIRTIO;
  bool rtl = selected.kind == NET_DRIVER_KIND_RTL8111;
  reply->interface_class = NET_INTERFACE_CLASS_ETHERNET;
  reply->driver = virtio ? NET_DRIVER_VIRTIO : rtl ? NET_DRIVER_RTL8111 : NET_DRIVER_CDC_NCM;
  if (virtio ? virtio_net_prepared(selected.virtio) : rtl ?
      rtl8111_prepared(selected.rtl) : usb_ncm_prepared(selected.ncm)) {
    reply->flags |= NET_CONTROLLER_PREPARED;
  }
  bool up;
  if (virtio ? virtio_net_carrier(selected.virtio, &up) :
      rtl ? rtl8111_carrier(selected.rtl, &up) : usb_ncm_carrier(selected.ncm, &up)) {
    reply->flags |= NET_CONTROLLER_CARRIER_KNOWN;
    if (up) {
      reply->flags |= NET_CONTROLLER_LINK_UP;
    }
  }
  for (struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if (same_controller(selected, interface->driver)) {
      reply->flags |= NET_CONTROLLER_BOUND;
      break;
    }
  }
  const uint8_t *mac = virtio ? virtio_net_identity_mac(selected.virtio) :
    rtl ? rtl8111_identity_mac(selected.rtl) : usb_ncm_identity_mac(selected.ncm);
  if (mac) {
    memcpy(reply->mac, mac, sizeof(reply->mac));
  }
}

void net_driver_interface_snapshot(const struct net_interface *iface,
    struct net_interface_reply *reply)
{
  net_worker_assert_context();
  struct net_driver_binding controller = iface->driver;
  if (controller.kind == NET_DRIVER_KIND_NONE) {
    return;
  }
  bool prepared, ready, carrier_known, carrier_up;
  const uint8_t *mac;
  if (controller.kind == NET_DRIVER_KIND_VIRTIO) {
    reply->driver = NET_DRIVER_VIRTIO;
    reply->controller_id = virtio_net_controller_id(controller.virtio);
    prepared = virtio_net_prepared(controller.virtio);
    ready = virtio_net_ready(controller.virtio);
    carrier_known = virtio_net_carrier(controller.virtio, &carrier_up);
    mac = virtio_net_identity_mac(controller.virtio);
  } else if (controller.kind == NET_DRIVER_KIND_RTL8111) {
    reply->driver = NET_DRIVER_RTL8111;
    reply->controller_id = rtl8111_controller_id(controller.rtl);
    prepared = rtl8111_prepared(controller.rtl);
    ready = rtl8111_ready(controller.rtl);
    carrier_known = rtl8111_carrier(controller.rtl, &carrier_up);
    mac = rtl8111_identity_mac(controller.rtl);
  } else {
    reply->driver = NET_DRIVER_CDC_NCM;
    reply->controller_id = usb_ncm_controller_id(controller.ncm);
    prepared = usb_ncm_prepared(controller.ncm);
    ready = usb_ncm_ready(controller.ncm);
    carrier_known = usb_ncm_carrier(controller.ncm, &carrier_up);
    mac = usb_ncm_identity_mac(controller.ncm);
  }
  reply->flags |= NET_INTERFACE_BOUND;
  if (prepared) {
    reply->flags |= NET_INTERFACE_PREPARED;
  }
  if (ready) {
    reply->flags |= NET_INTERFACE_READY;
  }
  if (carrier_known) {
    reply->flags |= NET_INTERFACE_CARRIER_KNOWN;
    if (carrier_up) {
      reply->flags |= NET_INTERFACE_LINK_UP;
    }
  }
  if (mac) {
    memcpy(reply->mac, mac, sizeof(reply->mac));
    reply->flags |= NET_INTERFACE_MAC_VALID;
  }
}

bool net_driver_service(const struct net_interface *iface)
{
  net_worker_assert_context();
  return iface->driver.kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_service(iface, iface->driver.virtio) :
    iface->driver.kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_service(iface, iface->driver.rtl) :
    iface->driver.kind == NET_DRIVER_KIND_USB_NCM && usb_ncm_service(iface, iface->driver.ncm);
}

bool net_driver_next_deadline(const struct net_interface *iface,
    uint64_t *deadline)
{
  return iface->driver.kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_next_deadline(iface->driver.virtio, deadline) :
    iface->driver.kind == NET_DRIVER_KIND_RTL8111 && rtl8111_next_deadline(iface->driver.rtl, deadline);
}

const uint8_t *net_driver_mac(const struct net_interface *iface)
{
  return iface->driver.kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_mac(iface->driver.virtio) :
    iface->driver.kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_mac(iface->driver.rtl) :
    iface->driver.kind == NET_DRIVER_KIND_USB_NCM ? usb_ncm_mac(iface->driver.ncm) : NULL;
}

bool net_driver_available(const struct net_interface *iface)
{
  return iface->driver.kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_available(iface->driver.virtio) :
    iface->driver.kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_available(iface->driver.rtl) :
    iface->driver.kind == NET_DRIVER_KIND_USB_NCM && usb_ncm_available(iface->driver.ncm);
}

enum net_result net_driver_transmit(const struct net_interface *iface,
    const void *frame, size_t length)
{
  return iface->driver.kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_transmit(iface->driver.virtio, frame, length) :
    iface->driver.kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_transmit(iface->driver.rtl, frame, length) :
    iface->driver.kind == NET_DRIVER_KIND_USB_NCM ? usb_ncm_transmit(iface->driver.ncm, frame, length) : NET_UNAVAILABLE;
}

bool net_driver_debug_ready(const struct net_interface *iface,
    struct net_debug_device *device)
{
  net_worker_assert_context();
  if (!iface || iface != debug_interface || !device) {
    return false;
  }
  *device = (struct net_debug_device){0};
  return iface->driver.kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_debug_ready(iface->driver.virtio, device) :
    iface->driver.kind == NET_DRIVER_KIND_RTL8111 && rtl8111_debug_ready(iface->driver.rtl, device);
}

bool net_driver_debug_service(const struct net_interface *iface)
{
  net_worker_assert_context();
  if (!iface) {
    return false;
  }
  if (iface != debug_interface) {
    return net_driver_service(iface);
  }
  return iface->driver.kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_debug_service(iface, iface->driver.virtio) :
    iface->driver.kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_debug_service(iface, iface->driver.rtl) :
    iface->driver.kind == NET_DRIVER_KIND_USB_NCM && usb_ncm_service(iface, iface->driver.ncm);
}

static enum net_driver_kind debug_kind(const struct net_interface *iface)
{
  return iface && iface == debug_interface ?
    iface->driver.kind : NET_DRIVER_KIND_NONE;
}

enum net_debug_status net_driver_debug_begin(const struct net_interface *iface,
    uint64_t generation)
{
  if (iface != debug_interface || !generation) {
    return NET_DEBUG_UNAVAILABLE;
  }
  enum net_driver_kind kind = debug_kind(iface);
  return kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_debug_begin(iface->driver.virtio, generation) :
    kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_debug_begin(iface->driver.rtl, generation) :
    NET_DEBUG_UNAVAILABLE;
}

enum net_debug_status net_driver_debug_poll(const struct net_interface *iface,
    uint64_t generation,
    void *frame, size_t capacity, size_t *length)
{
  *length = 0;
  enum net_driver_kind kind = debug_kind(iface);
  return kind == NET_DRIVER_KIND_VIRTIO ?
    virtio_net_debug_poll(iface->driver.virtio, generation, frame, capacity, length) :
    kind == NET_DRIVER_KIND_RTL8111 ?
    rtl8111_debug_poll(iface->driver.rtl, generation, frame, capacity, length) :
    NET_DEBUG_UNAVAILABLE;
}

enum net_debug_status net_driver_debug_transmit(const struct net_interface *iface,
    uint64_t generation,
    const void *frame, size_t length)
{
  enum net_driver_kind kind = debug_kind(iface);
  return kind == NET_DRIVER_KIND_VIRTIO ?
    virtio_net_debug_transmit(iface->driver.virtio, generation, frame, length) :
    kind == NET_DRIVER_KIND_RTL8111 ?
    rtl8111_debug_transmit(iface->driver.rtl, generation, frame, length) :
    NET_DEBUG_UNAVAILABLE;
}

enum net_debug_status net_driver_debug_restore(const struct net_interface *iface,
    uint64_t generation)
{
  enum net_driver_kind kind = debug_kind(iface);
  return kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_debug_restore(iface->driver.virtio, generation) :
    kind == NET_DRIVER_KIND_RTL8111 ? rtl8111_debug_restore(iface->driver.rtl, generation) :
    NET_DEBUG_UNAVAILABLE;
}

bool net_driver_debug_retained(const struct net_interface *iface)
{
  enum net_driver_kind kind = debug_kind(iface);
  return kind == NET_DRIVER_KIND_VIRTIO ? virtio_net_debug_retained(iface->driver.virtio) :
    kind == NET_DRIVER_KIND_RTL8111 && rtl8111_debug_retained(iface->driver.rtl);
}
