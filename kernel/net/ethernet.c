#include <kernel/memory.h>
#include <kernel/net/arp.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/driver.h>
#include "wire.h"

#define ETHERNET_FRAME_MIN 60 /* Padding excludes the hardware-supplied FCS. */
#define ETHERNET_GROUP_BIT 1u

struct ethernet_header {
  uint8_t destination[ETHERNET_ADDRESS_BYTES];
  uint8_t source[ETHERNET_ADDRESS_BYTES];
  uint8_t type[2];
};
static_assert(sizeof(struct ethernet_header) == ETHERNET_HEADER_BYTES);

const struct net_interface net_ethernet = { .name = "net0", .mtu = NET_PACKET_MAX_BYTES };
const uint8_t net_ethernet_broadcast[ETHERNET_ADDRESS_BYTES] = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static struct {
  uint64_t received, malformed, foreign, unsupported;
} ethernet_stats;

bool net_ethernet_is_unicast(const uint8_t address[ETHERNET_ADDRESS_BYTES])
{
  unsigned nonzero = 0;
  for (size_t i = 0; i < ETHERNET_ADDRESS_BYTES; ++i) {
    nonzero |= address[i];
  }
  return nonzero && !(address[0] & ETHERNET_GROUP_BIT);
}

enum net_result net_ethernet_transmit(const uint8_t destination[ETHERNET_ADDRESS_BYTES],
    uint16_t type, const void *payload, size_t length)
{
  if (length > net_ethernet.mtu || (length && !payload)) {
    return NET_INVALID;
  }
  const uint8_t *mac = net_driver_mac();
  if (!mac || !net_driver_available()) {
    return NET_UNAVAILABLE;
  }

  uint8_t frame[ETHERNET_FRAME_MAX];
  struct ethernet_header *header = (void *)frame;
  memcpy(header->destination, destination, ETHERNET_ADDRESS_BYTES);
  memcpy(header->source, mac, ETHERNET_ADDRESS_BYTES);
  net_write_u16(header->type, type);
  memcpy(frame + sizeof(*header), payload, length);
  size_t frame_length = sizeof(*header) + length;
  if (frame_length < ETHERNET_FRAME_MIN) {
    memset(frame + frame_length, 0, ETHERNET_FRAME_MIN - frame_length);
    frame_length = ETHERNET_FRAME_MIN;
  }
  return net_driver_transmit(frame, frame_length);
}

void net_ethernet_receive(const uint8_t *frame, size_t length)
{
  ++ethernet_stats.received;
  if (length < ETHERNET_HEADER_BYTES || length > ETHERNET_FRAME_MAX) {
    ++ethernet_stats.malformed;
    return;
  }
  const uint8_t *mac = net_driver_mac();
  if (!mac || !net_driver_available()) {
    return;
  }
  const struct ethernet_header *header = (const void *)frame;
  if (!net_ethernet_is_unicast(header->source)) {
    ++ethernet_stats.malformed;
    return;
  }
  bool own = !memcmp(header->destination, mac, ETHERNET_ADDRESS_BYTES);
  bool broadcast = !memcmp(header->destination, net_ethernet_broadcast, ETHERNET_ADDRESS_BYTES);
  if (!own && !broadcast) {
    ++ethernet_stats.foreign;
    return;
  }

  const uint8_t *payload = frame + sizeof(*header);
  size_t payload_length = length - sizeof(*header);
  switch (net_read_u16(header->type)) {
  case ETHERNET_TYPE_ARP:
    net_arp_receive(header->source, payload, payload_length);
    break;
  case ETHERNET_TYPE_IPV4:
    if (own) {
      net_ipv4_receive(&net_ethernet, payload, payload_length);
    } else {
      ++ethernet_stats.foreign;
    }
    break;
  default:
    ++ethernet_stats.unsupported;
    break;
  }
}
