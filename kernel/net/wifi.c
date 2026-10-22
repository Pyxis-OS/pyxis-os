#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/log_udp.h>
#include <kernel/net/wifi.h>
#include <kernel/wifi.h>

#define WIFI_NETWORK_RECEIVE_BUDGET 4u

static uint8_t identity_mac[ETHERNET_ADDRESS_BYTES];

static struct wifi_network_snapshot radio_snapshot(void)
{
  net_worker_assert_context();
  struct wifi_network_snapshot snapshot;
  uint64_t flags = cpu_save_interrupts();
  wifi_driver_network_snapshot(&snapshot);
  cpu_restore_interrupts(flags);
  return snapshot;
}

void net_wifi_inventory(struct net_controller_reply *reply)
{
  struct wifi_network_snapshot snapshot = radio_snapshot();
  *reply = (struct net_controller_reply){0};
  if (!snapshot.present) {
    return;
  }
  reply->controller_id = snapshot.controller_id;
  reply->driver = NET_DRIVER_AX200;
  reply->interface_class = NET_INTERFACE_CLASS_WIFI;
  if (snapshot.prepared) {
    reply->flags |= NET_CONTROLLER_PREPARED | NET_CONTROLLER_CARRIER_KNOWN;
  }
  if (snapshot.ready) {
    reply->flags |= NET_CONTROLLER_LINK_UP;
  }
  if (snapshot.mac_valid) {
    memcpy(reply->mac, snapshot.mac, sizeof(reply->mac));
  }
}

const uint8_t *net_wifi_mac(void)
{
  struct wifi_network_snapshot snapshot = radio_snapshot();
  if (!snapshot.mac_valid) {
    return NULL;
  }
  memcpy(identity_mac, snapshot.mac, sizeof(identity_mac));
  return identity_mac;
}

bool net_wifi_generation_valid(const struct net_interface *interface,
    uint64_t generation, bool require_ready)
{
  struct wifi_network_snapshot snapshot = radio_snapshot();
  return !net_log_udp_panicking() && generation &&
      generation == interface->connection_generation &&
      generation == snapshot.generation && (!require_ready ||
      (interface->connection_ready && snapshot.ready));
}

bool net_wifi_available(const struct net_interface *interface)
{
  return net_wifi_generation_valid(interface, interface->connection_generation, true);
}

bool net_wifi_sync(struct net_interface *interface)
{
  net_worker_assert_context();
  if (interface->driver.kind != NET_DRIVER_KIND_AX200) {
    return false;
  }
  struct wifi_network_snapshot snapshot = radio_snapshot();
  bool ready = snapshot.ready && snapshot.generation;
  if (interface->connection_generation == snapshot.generation &&
      interface->connection_ready == ready) {
    return false;
  }
  /* Radio publication closes data admission before this worker retires L3.
   * Clear on every incarnation change, including addressless retired attempts. */
  interface->connection_ready = false;
  net_ipv4_clear(interface);
  interface->dns_server = 0;
  interface->connection_generation = snapshot.generation;
  interface->connection_ready = ready;
  return true;
}

void net_wifi_interface_snapshot(const struct net_interface *interface,
    struct net_interface_reply *reply)
{
  struct wifi_network_snapshot snapshot = radio_snapshot();
  reply->driver = NET_DRIVER_AX200;
  reply->controller_id = snapshot.controller_id;
  reply->flags |= NET_INTERFACE_BOUND;
  if (snapshot.prepared) {
    reply->flags |= NET_INTERFACE_PREPARED | NET_INTERFACE_CARRIER_KNOWN;
  }
  if (!net_log_udp_panicking() && snapshot.ready && interface->connection_ready &&
      snapshot.generation && snapshot.generation == interface->connection_generation) {
    reply->flags |= NET_INTERFACE_READY | NET_INTERFACE_LINK_UP | NET_INTERFACE_AUTHENTICATION_VALID;
    reply->flags |= snapshot.security == WIFI_SECURITY_OPEN ? NET_INTERFACE_OPEN : NET_INTERFACE_AUTHENTICATED;
  }
  if (snapshot.mac_valid) {
    memcpy(reply->mac, snapshot.mac, sizeof(reply->mac));
    reply->flags |= NET_INTERFACE_MAC_VALID;
  }
}

void net_wifi_start(const struct net_interface *interface)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  wifi_driver_network_bind(interface->id);
  cpu_restore_interrupts(flags);
}

enum net_result net_wifi_transmit(const struct net_interface *interface,
    const void *frame, size_t length)
{
  net_worker_assert_context();
  if (!net_wifi_available(interface)) {
    return NET_UNAVAILABLE;
  }
  uint64_t flags = cpu_save_interrupts();
  enum net_result result = wifi_driver_transmit(interface->connection_generation, frame, length);
  cpu_restore_interrupts(flags);
  return result;
}

bool net_wifi_service(struct net_interface *interface)
{
  net_worker_assert_context();
  net_wifi_sync(interface);
  uint8_t frame[ETHERNET_FRAME_MAX];
  for (unsigned count = 0; count < WIFI_NETWORK_RECEIVE_BUDGET; ++count) {
    size_t length = 0;
    uint64_t flags = cpu_save_interrupts();
    bool received = wifi_driver_receive(interface->connection_generation,
        frame, sizeof(frame), &length);
    cpu_restore_interrupts(flags);
    if (!received) {
      return false;
    }
    if (length >= ETHERNET_HEADER_BYTES && length <= sizeof(frame)) {
      net_ethernet_receive(interface, frame, length);
    }
  }
  return true;
}
