#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/net/arp.h>
#include <kernel/net/echo.h>
#include <kernel/net/ipv4.h>
#include <kernel/task.h>
#include <kernel/virtio/net.h>
#include "wire.h"

#define ARP_NEIGHBOR_LIMIT 16
#define ARP_PENDING_LIMIT 16
#define ARP_PROBE_LIMIT 3
#define ARP_PROBE_INTERVAL_MS 1000
#define ARP_CACHE_LIFETIME_MS 60000
#define ARP_HARDWARE_ETHERNET 1
#define ARP_IPV4_BYTES 4
#define ARP_REQUEST 1
#define ARP_REPLY 2

enum neighbor_state { NEIGHBOR_EMPTY, NEIGHBOR_INCOMPLETE, NEIGHBOR_RESOLVED };

struct arp_neighbor {
  enum neighbor_state state;
  uint32_t address;
  uint8_t mac[ETHERNET_ADDRESS_BYTES];
  unsigned probes;
  uint64_t next_probe, expires, last_used;
};

struct arp_pending {
  struct net_packet *packet;
  uint32_t next_hop;
  uint64_t deadline, token;
};

struct arp_message {
  uint8_t hardware[2], protocol[2];
  uint8_t hardware_length, protocol_length;
  uint8_t operation[2];
  uint8_t sender_mac[ETHERNET_ADDRESS_BYTES], sender_ip[ARP_IPV4_BYTES];
  uint8_t target_mac[ETHERNET_ADDRESS_BYTES], target_ip[ARP_IPV4_BYTES];
};
static_assert(sizeof(struct arp_message) == 28);

/* Only the network worker touches these. Queued packets remain part of the
 * shared 32-packet software budget; the cache never holds borrowed RX bytes. */
static struct arp_neighbor neighbors[ARP_NEIGHBOR_LIMIT];
static struct arp_pending pending[ARP_PENDING_LIMIT];
static bool link_available;
static struct {
  uint64_t received, malformed, ignored, probes, replies, expired, queue_full;
} arp_stats;

static bool has_pending(uint32_t address)
{
  for (size_t i = 0; i < ARP_PENDING_LIMIT; ++i) {
    if (pending[i].packet && pending[i].next_hop == address) {
      return true;
    }
  }
  return false;
}

static struct arp_neighbor *find_neighbor(uint32_t address)
{
  for (size_t i = 0; i < ARP_NEIGHBOR_LIMIT; ++i) {
    if (neighbors[i].state != NEIGHBOR_EMPTY && neighbors[i].address == address) {
      return &neighbors[i];
    }
  }
  return NULL;
}

static struct arp_neighbor *reserve_neighbor(uint32_t address)
{
  struct arp_neighbor *entry = find_neighbor(address);
  if (entry) {
    return entry;
  }
  struct arp_neighbor *oldest = NULL;
  for (size_t i = 0; i < ARP_NEIGHBOR_LIMIT; ++i) {
    entry = &neighbors[i];
    if (entry->state == NEIGHBOR_EMPTY) {
      oldest = entry;
      break;
    }
    if (has_pending(entry->address)) {
      continue;
    }
    /* Keep a just-probed unresolved entry until its cooldown ends, even if
     * its last caller expired, so a new caller cannot force rapid retries. */
    if (entry->state == NEIGHBOR_INCOMPLETE && !task_deadline_expired(entry->next_probe)) {
      continue;
    }
    if (entry->state == NEIGHBOR_RESOLVED && task_deadline_expired(entry->expires)) {
      oldest = entry;
      break;
    }
    if (!oldest || entry->last_used < oldest->last_used) {
      oldest = entry;
    }
  }
  if (oldest) {
    *oldest = (struct arp_neighbor){ .state = NEIGHBOR_INCOMPLETE, .address = address };
  }
  return oldest;
}

static void release_packet(struct net_packet *packet)
{
  uint64_t flags = cpu_save_interrupts();
  net_packet_release(packet);
  cpu_restore_interrupts(flags);
}

static void finish_pending(struct arp_pending *entry, enum net_result result)
{
  uint64_t token = entry->token;
  release_packet(entry->packet);
  *entry = (struct arp_pending){0};
  if (result == NET_OK) {
    net_echo_transmitted(token);
  } else {
    net_echo_failed(token, result);
  }
}

void net_arp_clear(enum net_result reason)
{
  for (size_t i = 0; i < ARP_PENDING_LIMIT; ++i) {
    if (pending[i].packet) {
      finish_pending(&pending[i], reason);
    }
  }
  memset(neighbors, 0, sizeof(neighbors));
}

static enum net_result send_arp(uint16_t operation, uint32_t target,
    const uint8_t destination[ETHERNET_ADDRESS_BYTES])
{
  const uint8_t *mac = virtio_net_mac();
  if (!mac || !net_ipv4_address()) {
    return NET_UNAVAILABLE;
  }
  struct arp_message message = {
    .hardware_length = ETHERNET_ADDRESS_BYTES, .protocol_length = ARP_IPV4_BYTES,
  };
  net_write_u16(message.hardware, ARP_HARDWARE_ETHERNET);
  net_write_u16(message.protocol, ETHERNET_TYPE_IPV4);
  net_write_u16(message.operation, operation);
  memcpy(message.sender_mac, mac, ETHERNET_ADDRESS_BYTES);
  net_write_u32(message.sender_ip, net_ipv4_address());
  if (operation == ARP_REPLY) {
    memcpy(message.target_mac, destination, ETHERNET_ADDRESS_BYTES);
  }
  net_write_u32(message.target_ip, target);
  return net_ethernet_transmit(destination, ETHERNET_TYPE_ARP, &message, sizeof(message));
}

enum net_result net_arp_transmit(struct net_packet *packet, uint32_t next_hop,
    uint64_t deadline, uint64_t token)
{
  if (task_deadline_expired(deadline)) {
    return NET_TIMED_OUT;
  }
  if (!virtio_net_available()) {
    return NET_UNAVAILABLE;
  }
  struct arp_neighbor *neighbor = find_neighbor(next_hop);
  if (neighbor && neighbor->state == NEIGHBOR_RESOLVED &&
      !task_deadline_expired(neighbor->expires)) {
    enum net_result result = net_ethernet_transmit(neighbor->mac,
        ETHERNET_TYPE_IPV4, packet->data, packet->length);
    if (result == NET_OK) {
      neighbor->last_used = arch_monotonic_ns();
      release_packet(packet);
      net_echo_transmitted(token);
    }
    return result;
  }

  struct arp_pending *slot = NULL;
  for (size_t i = 0; i < ARP_PENDING_LIMIT; ++i) {
    if (!pending[i].packet) {
      slot = &pending[i];
      break;
    }
  }
  if (!slot || !(neighbor = reserve_neighbor(next_hop))) {
    ++arp_stats.queue_full;
    return NET_QUEUE_FULL;
  }
  if (neighbor->state == NEIGHBOR_RESOLVED ||
      (!has_pending(next_hop) && task_deadline_expired(neighbor->next_probe))) {
    *neighbor = (struct arp_neighbor){ .state = NEIGHBOR_INCOMPLETE, .address = next_hop };
  }
  neighbor->last_used = arch_monotonic_ns();
  *slot = (struct arp_pending){
    .packet = packet, .next_hop = next_hop, .deadline = deadline, .token = token,
  };
  return NET_OK;
}

void net_arp_receive(const uint8_t source[ETHERNET_ADDRESS_BYTES],
    const uint8_t *data, size_t length)
{
  ++arp_stats.received;
  if (length < sizeof(struct arp_message)) {
    ++arp_stats.malformed;
    return;
  }
  const struct arp_message *message = (const void *)data;
  uint16_t operation = net_read_u16(message->operation);
  if (net_read_u16(message->hardware) != ARP_HARDWARE_ETHERNET ||
      net_read_u16(message->protocol) != ETHERNET_TYPE_IPV4 ||
      message->hardware_length != ETHERNET_ADDRESS_BYTES || message->protocol_length != ARP_IPV4_BYTES ||
      (operation != ARP_REQUEST && operation != ARP_REPLY) ||
      memcmp(message->sender_mac, source, ETHERNET_ADDRESS_BYTES)) {
    ++arp_stats.malformed;
    return;
  }
  uint32_t sender = net_read_u32(message->sender_ip);
  const uint8_t *mac = virtio_net_mac();
  if (!mac || !net_ipv4_address() || net_read_u32(message->target_ip) != net_ipv4_address() ||
      sender == net_ipv4_address() ||
      (sender && !net_ipv4_is_neighbor(sender)) ||
      (operation == ARP_REPLY && (!sender || memcmp(message->target_mac, mac, ETHERNET_ADDRESS_BYTES)))) {
    ++arp_stats.ignored;
    return;
  }

  /* Learn requests addressed to us, and replies to existing resolutions.
   * Ignore unsolicited replies and gratuitous announcements; ARP is still
   * unauthenticated and this filtering does not make it an authority boundary. */
  struct arp_neighbor *neighbor = sender ? find_neighbor(sender) : NULL;
  if (sender && !neighbor && operation == ARP_REQUEST) {
    neighbor = reserve_neighbor(sender);
  }
  if (neighbor) {
    neighbor->state = NEIGHBOR_RESOLVED;
    memcpy(neighbor->mac, source, ETHERNET_ADDRESS_BYTES);
    neighbor->expires = task_deadline_after_ms(ARP_CACHE_LIFETIME_MS);
    neighbor->last_used = arch_monotonic_ns();
  }
  /* A sender of 0.0.0.0 is an address probe: answer it without caching zero. */
  if (operation == ARP_REQUEST && send_arp(ARP_REPLY, sender, source) == NET_OK) {
    ++arp_stats.replies;
  }
}

void net_arp_service(void)
{
  bool available = virtio_net_available();
  if (!available) {
    if (link_available) {
      net_arp_clear(NET_UNAVAILABLE);
      net_echo_invalidate(false);
    }
    link_available = false;
    return;
  }
  link_available = true;

  /* Expire before flushing resolved neighbors: a late reply must never send
   * a packet whose application deadline passed while resolution was pending. */
  for (size_t i = 0; i < ARP_PENDING_LIMIT; ++i) {
    if (pending[i].packet && task_deadline_expired(pending[i].deadline)) {
      ++arp_stats.expired;
      finish_pending(&pending[i], NET_TIMED_OUT);
    }
  }
  for (size_t i = 0; i < ARP_NEIGHBOR_LIMIT; ++i) {
    struct arp_neighbor *neighbor = &neighbors[i];
    if (neighbor->state == NEIGHBOR_EMPTY || !has_pending(neighbor->address)) {
      continue;
    }
    if (neighbor->state == NEIGHBOR_RESOLVED && task_deadline_expired(neighbor->expires)) {
      neighbor->state = NEIGHBOR_INCOMPLETE;
      neighbor->probes = 0;
      neighbor->next_probe = 0;
    }
    if (neighbor->state != NEIGHBOR_INCOMPLETE || !task_deadline_expired(neighbor->next_probe)) {
      continue;
    }
    if (neighbor->probes == ARP_PROBE_LIMIT) {
      for (size_t j = 0; j < ARP_PENDING_LIMIT; ++j) {
        if (pending[j].packet && pending[j].next_hop == neighbor->address) {
          finish_pending(&pending[j], NET_TIMED_OUT);
        }
      }
      continue;
    }
    enum net_result result = send_arp(ARP_REQUEST, neighbor->address, net_ethernet_broadcast);
    neighbor->next_probe = task_deadline_after_ms(ARP_PROBE_INTERVAL_MS);
    if (result == NET_OK) {
      ++neighbor->probes;
      ++arp_stats.probes;
    }
  }
  for (size_t i = 0; i < ARP_PENDING_LIMIT; ++i) {
    struct arp_pending *entry = &pending[i];
    if (!entry->packet) {
      continue;
    }
    struct arp_neighbor *neighbor = find_neighbor(entry->next_hop);
    if (neighbor->state != NEIGHBOR_RESOLVED) {
      continue;
    }
    /* Check again after the bounded probe batch, which can itself be preempted. */
    if (task_deadline_expired(entry->deadline)) {
      finish_pending(entry, NET_TIMED_OUT);
      continue;
    }
    enum net_result result = net_ethernet_transmit(neighbor->mac, ETHERNET_TYPE_IPV4,
        entry->packet->data, entry->packet->length);
    if (result != NET_QUEUE_FULL) {
      neighbor->last_used = arch_monotonic_ns();
      finish_pending(entry, result);
    }
  }
}

bool net_arp_next_deadline(uint64_t *deadline)
{
  bool found = false;
  uint64_t next = UINT64_MAX;
  for (size_t i = 0; i < ARP_PENDING_LIMIT; ++i) {
    if (!pending[i].packet) {
      continue;
    }
    found = true;
    if (pending[i].deadline < next) {
      next = pending[i].deadline;
    }
    struct arp_neighbor *neighbor = find_neighbor(pending[i].next_hop);
    if (neighbor->state == NEIGHBOR_INCOMPLETE && neighbor->next_probe < next) {
      next = neighbor->next_probe;
    }
  }
  *deadline = next;
  return found;
}
