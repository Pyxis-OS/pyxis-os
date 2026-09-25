#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/net/arp.h>
#include <kernel/memory.h>
#include <kernel/net/echo.h>
#include <kernel/net/icmp.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/udp.h>
#include <kernel/net/lwip.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/virtio/net.h>
#include "wire.h"

#define IPV4_VERSION 4
#define IPV4_VERSION_SHIFT 4
#define IPV4_HEADER_WORD_MASK 0x0f
#define IPV4_HEADER_WORD_BYTES 4
#define IPV4_DEFAULT_TTL 64
#define IPV4_MULTICAST_BASE UINT32_C(0xe0000000)
#define IPV4_LOOPBACK_MASK UINT32_C(0xff000000)
#define IPV4_FLAG_RESERVED 0x8000
#define IPV4_FLAG_DONT_FRAGMENT 0x4000
#define IPV4_FLAG_MORE_FRAGMENTS 0x2000
#define IPV4_FRAGMENT_OFFSET_MASK 0x1fff

struct ipv4_header {
  uint8_t version_length;
  uint8_t service;
  uint8_t total_length[2];
  uint8_t identification[2];
  uint8_t fragment[2];
  uint8_t ttl;
  uint8_t protocol;
  uint8_t checksum[2];
  uint8_t source[4];
  uint8_t destination[4];
};
static_assert(sizeof(struct ipv4_header) == IPV4_HEADER_SIZE);

/* Diagnostics owned by the sole network worker, not an application ABI. */
static struct {
  uint64_t received, malformed, unsupported, nonlocal;
} ipv4_stats;

bool net_ipv4_is_loopback(uint32_t address)
{
  return (address & IPV4_LOOPBACK_MASK) == (IPV4_LOOPBACK_ADDRESS & IPV4_LOOPBACK_MASK);
}

static struct {
  uint32_t address, mask, gateway;
  unsigned prefix;
} configuration;

static void assert_worker_context(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  cpu_restore_interrupts(flags);
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
}

static bool is_unicast(uint32_t address)
{
  return (address >> 24) != 0 && !net_ipv4_is_loopback(address) &&
      address < IPV4_MULTICAST_BASE;
}

static bool is_subnet_host(uint32_t address, uint32_t mask, unsigned prefix)
{
  /* /31 has two host addresses; /32 is a single-host route. Neither has a
   * directed broadcast or a reserved subnet-number address. */
  uint32_t host = address & ~mask;
  return prefix >= 31 || (host != 0 && host != ~mask);
}

uint32_t net_ipv4_address(void)
{
  return configuration.address;
}

bool net_ipv4_is_neighbor(uint32_t address)
{
  return configuration.address && is_unicast(address) &&
      (address & configuration.mask) == (configuration.address & configuration.mask) &&
      is_subnet_host(address, configuration.mask, configuration.prefix);
}

enum net_result net_ipv4_configure(uint32_t address, unsigned prefix, uint32_t gateway)
{
  assert_worker_context();
  if (prefix < 1 || prefix > 32 || !is_unicast(address)) {
    return NET_INVALID;
  }
  uint32_t mask = UINT32_MAX << (32 - prefix);
  if (!is_subnet_host(address, mask, prefix) ||
      (gateway && (!is_unicast(gateway) || gateway == address ||
       (gateway & mask) != (address & mask) || !is_subnet_host(gateway, mask, prefix)))) {
    return NET_INVALID;
  }
  if (!virtio_net_mac()) {
    return NET_UNAVAILABLE;
  }

  net_arp_clear(NET_UNAVAILABLE);
  net_echo_invalidate(true);
  if (configuration.address != address) {
    net_udp_invalidate_address(configuration.address);
  }
  configuration = (typeof(configuration)){
    .address = address, .mask = mask, .prefix = prefix, .gateway = gateway,
  };
  net_lwip_refresh_address();
  return NET_OK;
}

void net_ipv4_clear(void)
{
  assert_worker_context();
  net_arp_clear(NET_UNAVAILABLE);
  net_echo_invalidate(true);
  net_udp_invalidate_address(configuration.address);
  configuration = (typeof(configuration)){0};
  net_lwip_refresh_address();
}

enum net_result net_ipv4_route(uint32_t source, uint32_t destination,
    struct ipv4_route *route)
{
  uint32_t selected, next_hop = 0;
  if (net_ipv4_is_loopback(destination)) {
    selected = source ? source : IPV4_LOOPBACK_ADDRESS;
    if (!net_ipv4_is_loopback(selected)) {
      return NET_NO_ROUTE;
    }
  } else {
    if (!is_unicast(destination)) {
      return NET_INVALID;
    }
    if (!configuration.address) {
      return NET_NO_ROUTE;
    }
    selected = configuration.address;
    if (destination != selected) {
      if ((destination & configuration.mask) == (selected & configuration.mask)) {
        if (!net_ipv4_is_neighbor(destination)) {
          return NET_INVALID;
        }
        next_hop = destination;
      } else if (configuration.gateway) {
        next_hop = configuration.gateway;
      } else {
        return NET_NO_ROUTE;
      }
      if (!virtio_net_available()) {
        return NET_UNAVAILABLE;
      }
    }
    if (source && source != selected) {
      return NET_NO_ROUTE;
    }
  }
  *route = (struct ipv4_route){ .source = selected, .next_hop = next_hop };
  return NET_OK;
}

enum net_result net_ipv4_transmit(struct net_packet *packet, uint32_t source,
    uint32_t destination, uint8_t protocol, uint64_t deadline, struct ipv4_completion completion)
{
  assert_worker_context();
  if (!packet || packet->length < IPV4_HEADER_SIZE || packet->length > NET_PACKET_MAX_BYTES) {
    return NET_INVALID;
  }
  if (task_deadline_expired(deadline)) {
    return NET_TIMED_OUT;
  }
  struct ipv4_route route;
  enum net_result result = net_ipv4_route(source, destination, &route);
  if (result != NET_OK) {
    return result;
  }

  struct ipv4_header *header = (void *)packet->data;
  *header = (struct ipv4_header){
    .version_length = (IPV4_VERSION << IPV4_VERSION_SHIFT) |
        (IPV4_HEADER_SIZE / IPV4_HEADER_WORD_BYTES),
    .ttl = IPV4_DEFAULT_TTL,
    .protocol = protocol,
  };
  net_write_u16(header->total_length, packet->length);
  /* No fragmentation is supported. DF also makes a zero identification valid
   * for these atomic datagrams; receivers must not use it for reassembly. */
  net_write_u16(header->fragment, IPV4_FLAG_DONT_FRAGMENT);
  net_write_u32(header->source, route.source);
  net_write_u32(header->destination, destination);
  net_write_u16(header->checksum, net_checksum(packet->data, IPV4_HEADER_SIZE));
  return net_ipv4_submit(packet, deadline, completion);
}

enum net_result net_ipv4_submit(struct net_packet *packet, uint64_t deadline,
    struct ipv4_completion completion)
{
  assert_worker_context();
  if (!packet || packet->length < IPV4_HEADER_SIZE || packet->length > NET_PACKET_MAX_BYTES) {
    return NET_INVALID;
  }
  const struct ipv4_header *header = (const void *)packet->data;
  if (header->version_length != ((IPV4_VERSION << IPV4_VERSION_SHIFT) |
      (IPV4_HEADER_SIZE / IPV4_HEADER_WORD_BYTES)) ||
      net_read_u16(header->total_length) != packet->length ||
      (net_read_u16(header->fragment) & ~IPV4_FLAG_DONT_FRAGMENT) ||
      net_checksum(packet->data, IPV4_HEADER_SIZE)) {
    return NET_INVALID;
  }
  if (task_deadline_expired(deadline)) {
    return NET_TIMED_OUT;
  }
  uint32_t source = net_read_u32(header->source);
  struct ipv4_route route;
  enum net_result result = net_ipv4_route(source, net_read_u32(header->destination), &route);
  if (result != NET_OK) {
    return result;
  }
  if (source != route.source) {
    return NET_INVALID;
  }
  if (route.next_hop) {
    return net_arp_transmit(packet, route.next_hop, deadline, completion);
  }
  uint64_t flags = cpu_save_interrupts();
  result = net_transmit(&net_loopback, packet);
  cpu_restore_interrupts(flags);
  if (result == NET_OK) {
    net_ipv4_transmitted(completion);
  }
  return result;
}

void net_ipv4_cancel_tcp(uint64_t generation)
{
  net_worker_assert_context();
  KASSERT(generation);
  net_arp_cancel((struct ipv4_completion){ IPV4_NOTIFY_TCP, generation });
  net_loopback_cancel_tcp(generation);
}

void net_ipv4_receive(const struct net_interface *interface,
    const uint8_t *data, size_t length)
{
  ++ipv4_stats.received;
  if (length < IPV4_HEADER_SIZE) {
    ++ipv4_stats.malformed;
    return;
  }

  const struct ipv4_header *header = (const void *)data;
  size_t header_length = (header->version_length & IPV4_HEADER_WORD_MASK) *
      IPV4_HEADER_WORD_BYTES;
  size_t total_length = net_read_u16(header->total_length);
  if (header->version_length >> IPV4_VERSION_SHIFT != IPV4_VERSION ||
      header_length < IPV4_HEADER_SIZE || header_length > length ||
      total_length < header_length || total_length > length ||
      total_length > NET_PACKET_MAX_BYTES || net_checksum(data, header_length)) {
    ++ipv4_stats.malformed;
    return;
  }

  uint16_t fragment = net_read_u16(header->fragment);
  if (fragment & IPV4_FLAG_RESERVED) {
    ++ipv4_stats.malformed;
    return;
  }
  if (header_length != IPV4_HEADER_SIZE ||
      (fragment & (IPV4_FLAG_MORE_FRAGMENTS | IPV4_FRAGMENT_OFFSET_MASK))) {
    ++ipv4_stats.unsupported;
    return;
  }

  uint32_t source = net_read_u32(header->source);
  uint32_t destination = net_read_u32(header->destination);
  bool local_source = net_ipv4_is_loopback(source) ||
      (configuration.address && source == configuration.address);
  bool local_destination = net_ipv4_is_loopback(destination) ||
      (configuration.address && destination == configuration.address);
  bool accepted = interface == &net_loopback && local_source && local_destination;
  if (interface == &net_ethernet) {
    accepted = configuration.address && destination == configuration.address &&
        is_unicast(source) && source != configuration.address &&
        ((source & configuration.mask) != (configuration.address & configuration.mask) ||
         is_subnet_host(source, configuration.mask, configuration.prefix));
  }
  if (!accepted) {
    ++ipv4_stats.nonlocal;
    return;
  }

  /* This host delivers locally, never forwards. Do not decrement TTL or reject
   * an otherwise valid local packet merely because its TTL is below two. */
  switch (header->protocol) {
  case IPV4_PROTOCOL_ICMP:
    net_icmp_receive(source, destination, data + header_length, total_length - header_length);
    break;
  case IPV4_PROTOCOL_TCP:
    net_lwip_receive(data, total_length);
    break;
  case IPV4_PROTOCOL_UDP:
    net_udp_receive_packet(source, destination, data + header_length, total_length - header_length);
    break;
  default:
    ++ipv4_stats.unsupported;
    break;
  }
}

void net_ipv4_snapshot(struct net_config_reply *reply)
{
  assert_worker_context();
  *reply = (struct net_config_reply){
    .address = configuration.address, .prefix = configuration.prefix,
    .gateway = configuration.gateway, .mtu = net_ethernet.mtu,
  };
  if (virtio_net_present()) {
    reply->flags |= NET_CONFIG_PRESENT;
  }
  if (virtio_net_ready()) {
    reply->flags |= NET_CONFIG_READY;
  }
  if (virtio_net_available()) {
    reply->flags |= NET_CONFIG_LINK_UP;
  }
  if (configuration.address) {
    reply->flags |= NET_CONFIG_ASSIGNED;
  }
  const uint8_t *mac = virtio_net_mac();
  if (mac) {
    memcpy(reply->mac, mac, sizeof(reply->mac));
  }
}

void net_ipv4_transmitted(struct ipv4_completion completion)
{
  switch (completion.consumer) {
  case IPV4_NOTIFY_ECHO: net_echo_transmitted(completion.token); break;
  case IPV4_NOTIFY_UDP: net_udp_transmitted(completion.token); break;
  /* Queue acceptance/loss is not a peer ACK. lwIP owns retransmission. */
  case IPV4_NOTIFY_TCP:
  case IPV4_NOTIFY_NONE: break;
  }
}

void net_ipv4_failed(struct ipv4_completion completion, enum net_result result)
{
  switch (completion.consumer) {
  case IPV4_NOTIFY_ECHO: net_echo_failed(completion.token, result); break;
  case IPV4_NOTIFY_UDP: net_udp_failed(completion.token, result); break;
  /* Queue acceptance/loss is not a peer ACK. lwIP owns retransmission. */
  case IPV4_NOTIFY_TCP:
  case IPV4_NOTIFY_NONE: break;
  }
}
