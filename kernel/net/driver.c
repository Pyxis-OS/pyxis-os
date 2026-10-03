#include <kernel/memory.h>
#include <kernel/net/driver.h>
#include <kernel/net/ethernet.h>
#include <kernel/virtio/net.h>

/* The worker owns this binding; controller state stays with its driver. */
static struct virtio_net_controller *bound;

static enum call_status find_controller(const struct net_selector *selector,
    struct virtio_net_controller **controller)
{
  for (size_t i = 0; i < sizeof(selector->reserved); ++i) {
    if (selector->reserved[i]) {
      return CALL_BAD_REQUEST;
    }
  }
  if (selector->kind == NET_SELECT_VIRTIO) {
    for (size_t i = 0; i < sizeof(selector->mac); ++i) {
      if (selector->mac[i]) {
        return CALL_BAD_REQUEST;
      }
    }
  } else if (selector->kind != NET_SELECT_MAC ||
             !net_ethernet_is_unicast(selector->mac)) {
    return CALL_BAD_REQUEST;
  }
  if (!virtio_net_inventory_complete()) {
    return CALL_UNAVAILABLE;
  }

  struct virtio_net_controller *match = NULL;
  bool unknown_identity = false;
  for (struct virtio_net_controller *candidate = virtio_net_first(); candidate;
       candidate = virtio_net_next(candidate)) {
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
    if (match) {
      return CALL_BUSY;
    }
    match = candidate;
  }
  if (unknown_identity) {
    return CALL_UNAVAILABLE;
  }
  if (!match) {
    return CALL_NOT_FOUND;
  }
  *controller = match;
  return CALL_OK;
}

static void snapshot(struct virtio_net_controller *controller,
    struct net_config_reply *reply)
{
  if (!controller) {
    return;
  }
  reply->flags |= NET_CONFIG_PRESENT;
  if (controller == bound) {
    reply->flags |= NET_CONFIG_BOUND;
  }
  if (virtio_net_ready(controller)) {
    reply->flags |= NET_CONFIG_READY;
  }
  if (virtio_net_available(controller)) {
    reply->flags |= NET_CONFIG_LINK_UP;
  }
  const uint8_t *mac = virtio_net_mac(controller);
  if (mac) {
    memcpy(reply->mac, mac, sizeof(reply->mac));
  }
}

enum call_status net_driver_bind(const struct net_selector *selector)
{
  net_worker_assert_context();
  struct virtio_net_controller *controller;
  enum call_status status = find_controller(selector, &controller);
  if (status != CALL_OK) {
    return status;
  }
  if (bound) {
    return bound == controller ? CALL_OK : CALL_BUSY;
  }
  bound = controller;
  virtio_net_start(bound);
  return CALL_OK;
}

enum call_status net_driver_lookup(const struct net_selector *selector,
    struct net_config_reply *reply)
{
  net_worker_assert_context();
  struct virtio_net_controller *controller;
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
  return bound && virtio_net_service(bound);
}

bool net_driver_next_deadline(uint64_t *deadline)
{
  return bound && virtio_net_next_deadline(bound, deadline);
}

const uint8_t *net_driver_mac(void)
{
  return bound ? virtio_net_mac(bound) : NULL;
}

bool net_driver_available(void)
{
  return bound && virtio_net_available(bound);
}

enum net_result net_driver_transmit(const void *frame, size_t length)
{
  return bound ? virtio_net_transmit(bound, frame, length) : NET_UNAVAILABLE;
}
