#include <kernel/memory.h>
#include <kernel/net/driver.h>
#include <kernel/net/debug.h>
#include <kernel/net/panic_tx.h>
#include <kernel/net/ethernet.h>
#include <kernel/virtio/net.h>
#include <kernel/net/rtl8111.h>
#include <kernel/net/log_udp.h>
#include <stdatomic.h>

/* The worker owns this binding; controller state stays with its driver. */
enum driver_kind { DRIVER_NONE, DRIVER_VIRTIO, DRIVER_RTL8111 };
struct controller {
  enum driver_kind kind;
  union {
    struct virtio_net_controller *virtio;
    struct rtl8111_controller *rtl;
  };
};
static struct controller bound;
/* Publish the immutable selection before activation. Fatal entry can close
 * its driver gate even when activation is interrupted. */
static atomic_int panic_kind;
static atomic_bool handoff_enabled;
static uint32_t handoff_cpu;
static bool debug_enabled;

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

void net_driver_debug_enable(void)
{
  net_panic_context_enable();
  debug_enabled = true;
}

static bool same_controller(struct controller a, struct controller b)
{
  return a.kind == b.kind && (a.kind == DRIVER_VIRTIO ? a.virtio == b.virtio :
    a.kind == DRIVER_RTL8111 ? a.rtl == b.rtl : true);
}

static enum call_status find_controller(const struct net_selector *selector,
    struct controller *controller)
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
      (selector->kind != NET_SELECT_VIRTIO && !rtl8111_inventory_complete())) {
    return CALL_UNAVAILABLE;
  }

  struct controller match = {0};
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
    if (match.kind != DRIVER_NONE) {
      return CALL_BUSY;
    }
    match = (struct controller){.kind = DRIVER_VIRTIO, .virtio = candidate};
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
      if (match.kind != DRIVER_NONE) {
        return CALL_BUSY;
      }
      match = (struct controller){.kind = DRIVER_RTL8111, .rtl = candidate};
    }
  }
  if (unknown_identity) {
    return CALL_UNAVAILABLE;
  }
  if (match.kind == DRIVER_NONE) {
    return CALL_NOT_FOUND;
  }
  *controller = match;
  return CALL_OK;
}

static void snapshot(struct controller controller,
    struct net_config_reply *reply)
{
  if (controller.kind == DRIVER_NONE) {
    return;
  }
  reply->flags |= NET_CONFIG_PRESENT;
  if (same_controller(controller, bound)) {
    reply->flags |= NET_CONFIG_BOUND;
  }
  if (controller.kind == DRIVER_VIRTIO ? virtio_net_ready(controller.virtio) :
      rtl8111_ready(controller.rtl)) {
    reply->flags |= NET_CONFIG_READY;
  }
  if (controller.kind == DRIVER_VIRTIO ? virtio_net_available(controller.virtio) :
      rtl8111_available(controller.rtl)) {
    reply->flags |= NET_CONFIG_LINK_UP;
  }
  const uint8_t *mac = controller.kind == DRIVER_VIRTIO ?
    virtio_net_mac(controller.virtio) : rtl8111_mac(controller.rtl);
  if (mac) {
    memcpy(reply->mac, mac, sizeof(reply->mac));
  }
}

enum call_status net_driver_bind(const struct net_selector *selector)
{
  net_worker_assert_context();
  if (net_log_udp_panicking()) {
    return CALL_UNAVAILABLE;
  }
  struct controller controller;
  enum call_status status = find_controller(selector, &controller);
  if (status != CALL_OK) {
    return status;
  }
  if (bound.kind != DRIVER_NONE) {
    return same_controller(bound, controller) ? CALL_OK : CALL_BUSY;
  }
  if (selector->kind == NET_SELECT_LINKED_CONTROLLER) {
    bool up;
    bool known = controller.kind == DRIVER_VIRTIO ?
      virtio_net_carrier(controller.virtio, &up) :
      rtl8111_carrier(controller.rtl, &up);
    if (!known || !up) {
      return CALL_UNAVAILABLE;
    }
  }
  bound = controller;
  atomic_store_explicit(&panic_kind, bound.kind, memory_order_release);
  if (bound.kind == DRIVER_VIRTIO) {
    virtio_net_start(bound.virtio);
  } else {
    rtl8111_start(bound.rtl);
  }
  return CALL_OK;
}

bool net_driver_panic_begin(uint8_t mac[6])
{
  enum driver_kind kind = atomic_load_explicit(&panic_kind, memory_order_acquire);
  if (kind == DRIVER_NONE) {
    return false;
  }
  const uint8_t *identity = kind == DRIVER_VIRTIO ?
      virtio_net_identity_mac(bound.virtio) : rtl8111_identity_mac(bound.rtl);
  bool ready = kind == DRIVER_VIRTIO ? virtio_net_panic_begin(bound.virtio) :
      rtl8111_panic_begin(bound.rtl);
  if (!ready || !identity) {
    return false;
  }
  memcpy(mac, identity, 6);
  return true;
}

bool net_driver_panic_transmit(const void *frame, size_t length)
{
  enum driver_kind kind = atomic_load_explicit(&panic_kind, memory_order_acquire);
  return kind == DRIVER_VIRTIO ? virtio_net_panic_transmit(bound.virtio, frame, length) :
      kind == DRIVER_RTL8111 && rtl8111_panic_transmit(bound.rtl, frame, length);
}

void net_driver_next_controller(uint32_t after_id,
    struct net_controller_reply *reply)
{
  net_worker_assert_context();
  *reply = (struct net_controller_reply){0};
  if (virtio_net_inventory_complete() && rtl8111_inventory_complete()) {
    reply->flags = NET_CONTROLLER_INVENTORY_COMPLETE;
  }
  struct controller selected = {0};
  for (struct virtio_net_controller *candidate = virtio_net_first(); candidate;
       candidate = virtio_net_next(candidate)) {
    uint32_t id = virtio_net_controller_id(candidate);
    if (id > after_id && (!reply->controller_id || id < reply->controller_id)) {
      reply->controller_id = id;
      selected = (struct controller){.kind = DRIVER_VIRTIO, .virtio = candidate};
    }
  }
  for (struct rtl8111_controller *candidate = rtl8111_first(); candidate;
       candidate = rtl8111_next(candidate)) {
    uint32_t id = rtl8111_controller_id(candidate);
    if (id > after_id && (!reply->controller_id || id < reply->controller_id)) {
      reply->controller_id = id;
      selected = (struct controller){.kind = DRIVER_RTL8111, .rtl = candidate};
    }
  }
  if (selected.kind == DRIVER_NONE) {
    return;
  }
  bool virtio = selected.kind == DRIVER_VIRTIO;
  reply->driver = virtio ? NET_DRIVER_VIRTIO : NET_DRIVER_RTL8111;
  if (virtio ? virtio_net_prepared(selected.virtio) : rtl8111_prepared(selected.rtl)) {
    reply->flags |= NET_CONTROLLER_PREPARED;
  }
  bool up;
  if (virtio ? virtio_net_carrier(selected.virtio, &up) :
      rtl8111_carrier(selected.rtl, &up)) {
    reply->flags |= NET_CONTROLLER_CARRIER_KNOWN;
    if (up) {
      reply->flags |= NET_CONTROLLER_LINK_UP;
    }
  }
  if (same_controller(selected, bound)) {
    reply->flags |= NET_CONTROLLER_BOUND;
  }
  const uint8_t *mac = virtio ? virtio_net_identity_mac(selected.virtio) :
    rtl8111_identity_mac(selected.rtl);
  if (mac) {
    memcpy(reply->mac, mac, sizeof(reply->mac));
  }
}

enum call_status net_driver_lookup(const struct net_selector *selector,
    struct net_config_reply *reply)
{
  net_worker_assert_context();
  struct controller controller;
  enum call_status status = find_controller(selector, &controller);
  if (status == CALL_OK) {
    *reply = (struct net_config_reply){.mtu = net_ethernet.mtu};
    snapshot(controller, reply);
  }
  return status;
}

void net_driver_snapshot(struct net_config_reply *reply)
{
  net_worker_assert_context();
  snapshot(bound, reply);
}

bool net_driver_service(void)
{
  net_worker_assert_context();
  return bound.kind == DRIVER_VIRTIO ? virtio_net_service(bound.virtio) :
    bound.kind == DRIVER_RTL8111 && rtl8111_service(bound.rtl);
}

bool net_driver_next_deadline(uint64_t *deadline)
{
  return bound.kind == DRIVER_VIRTIO ? virtio_net_next_deadline(bound.virtio, deadline) :
    bound.kind == DRIVER_RTL8111 && rtl8111_next_deadline(bound.rtl, deadline);
}

const uint8_t *net_driver_mac(void)
{
  return bound.kind == DRIVER_VIRTIO ? virtio_net_mac(bound.virtio) :
    bound.kind == DRIVER_RTL8111 ? rtl8111_mac(bound.rtl) : NULL;
}

bool net_driver_available(void)
{
  return bound.kind == DRIVER_VIRTIO ? virtio_net_available(bound.virtio) :
    bound.kind == DRIVER_RTL8111 && rtl8111_available(bound.rtl);
}

enum net_result net_driver_transmit(const void *frame, size_t length)
{
  return bound.kind == DRIVER_VIRTIO ? virtio_net_transmit(bound.virtio, frame, length) :
    bound.kind == DRIVER_RTL8111 ? rtl8111_transmit(bound.rtl, frame, length) : NET_UNAVAILABLE;
}

bool net_driver_debug_ready(struct net_debug_device *device)
{
  net_worker_assert_context();
  if (!debug_enabled || !device) {
    return false;
  }
  *device = (struct net_debug_device){0};
  return bound.kind == DRIVER_VIRTIO ? virtio_net_debug_ready(bound.virtio, device) :
    bound.kind == DRIVER_RTL8111 && rtl8111_debug_ready(bound.rtl, device);
}

bool net_driver_debug_service(void)
{
  net_worker_assert_context();
  return bound.kind == DRIVER_VIRTIO ? virtio_net_debug_service(bound.virtio) :
    bound.kind == DRIVER_RTL8111 && rtl8111_debug_service(bound.rtl);
}

enum net_debug_status net_driver_debug_begin(uint64_t generation)
{
  if (!debug_enabled || !generation) {
    return NET_DEBUG_UNAVAILABLE;
  }
  enum driver_kind kind = atomic_load_explicit(&panic_kind, memory_order_acquire);
  return kind == DRIVER_VIRTIO ? virtio_net_debug_begin(bound.virtio, generation) :
    kind == DRIVER_RTL8111 ? rtl8111_debug_begin(bound.rtl, generation) :
    NET_DEBUG_UNAVAILABLE;
}

enum net_debug_status net_driver_debug_poll(uint64_t generation,
    void *frame, size_t capacity, size_t *length)
{
  *length = 0;
  return bound.kind == DRIVER_VIRTIO ?
    virtio_net_debug_poll(bound.virtio, generation, frame, capacity, length) :
    bound.kind == DRIVER_RTL8111 ?
    rtl8111_debug_poll(bound.rtl, generation, frame, capacity, length) :
    NET_DEBUG_UNAVAILABLE;
}

enum net_debug_status net_driver_debug_transmit(uint64_t generation,
    const void *frame, size_t length)
{
  return bound.kind == DRIVER_VIRTIO ?
    virtio_net_debug_transmit(bound.virtio, generation, frame, length) :
    bound.kind == DRIVER_RTL8111 ?
    rtl8111_debug_transmit(bound.rtl, generation, frame, length) :
    NET_DEBUG_UNAVAILABLE;
}

enum net_debug_status net_driver_debug_restore(uint64_t generation)
{
  return bound.kind == DRIVER_VIRTIO ? virtio_net_debug_restore(bound.virtio, generation) :
    bound.kind == DRIVER_RTL8111 ? rtl8111_debug_restore(bound.rtl, generation) :
    NET_DEBUG_UNAVAILABLE;
}

bool net_driver_debug_retained(void)
{
  enum driver_kind kind = atomic_load_explicit(&panic_kind, memory_order_acquire);
  return kind == DRIVER_VIRTIO ? virtio_net_debug_retained(bound.virtio) :
    kind == DRIVER_RTL8111 && rtl8111_debug_retained(bound.rtl);
}
