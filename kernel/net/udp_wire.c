#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/net/ipv4.h>
#include "udp_internal.h"
#include "wire.h"

struct udp_header {
  uint8_t source[2], destination[2], length[2], checksum[2];
};
static_assert(sizeof(struct udp_header) == UDP_HEADER_SIZE);
static_assert(UDP_MAX_PAYLOAD + UDP_HEADER_SIZE + IPV4_HEADER_SIZE == NET_PACKET_MAX_BYTES);

static struct {
  uint64_t received, malformed, bad_checksum;
} udp_wire_stats;

static uint16_t udp_checksum(uint32_t source, uint32_t destination,
    const uint8_t *message, size_t length)
{
  /* The even-sized IPv4 pseudo-header precedes the UDP message. Uncomplement
   * its folded sum before adding pseudo-header words; net_checksum also handles
   * an odd payload's final high byte. The pseudo-header is never transmitted. */
  uint32_t sum = (uint16_t)~net_checksum(message, length);
  sum += source >> 16;
  sum += source & UINT16_MAX;
  sum += destination >> 16;
  sum += destination & UINT16_MAX;
  sum += IPV4_PROTOCOL_UDP + length;
  while (sum >> 16) {
    sum = (sum & UINT16_MAX) + (sum >> 16);
  }
  return (uint16_t)~sum;
}

enum net_result net_udp_send_packet(struct udp_endpoint *endpoint, uint32_t address,
    uint16_t port, const uint8_t *data, size_t length, uint64_t deadline, uint64_t token)
{
  uint32_t source = endpoint->local.address;
  if (endpoint->broadcast) {
    if (net_ipv4_is_loopback(address)) {
      return NET_NO_ROUTE;
    }
    source = net_ipv4_address();
    if (!source && address != IPV4_LIMITED_BROADCAST) {
      return NET_NO_ROUTE;
    }
  }
  uint64_t flags = cpu_save_interrupts();
  struct net_packet *packet = net_packet_allocate(IPV4_HEADER_SIZE + UDP_HEADER_SIZE + length);
  cpu_restore_interrupts(flags);
  if (!packet) {
    return NET_NO_MEMORY;
  }
  struct udp_header *header = (void *)(packet->data + IPV4_HEADER_SIZE);
  *header = (struct udp_header){0};
  net_write_u16(header->source, endpoint->local.port);
  net_write_u16(header->destination, port);
  net_write_u16(header->length, UDP_HEADER_SIZE + length);
  memcpy((uint8_t *)header + UDP_HEADER_SIZE, data, length);
  uint16_t checksum = udp_checksum(source, address,
      (const uint8_t *)header, UDP_HEADER_SIZE + length);
  /* Wire zero means omitted checksum; computed zero must be encoded as all ones. */
  net_write_u16(header->checksum, checksum ? checksum : UINT16_MAX);

  struct ipv4_completion completion = {IPV4_NOTIFY_UDP, token};
  enum net_result result;
  if (endpoint->broadcast && address == IPV4_LIMITED_BROADCAST) {
    result = net_ipv4_transmit_udp_broadcast(packet, source, deadline, completion);
  } else {
    result = net_ipv4_transmit(packet, source, address, IPV4_PROTOCOL_UDP, deadline, completion);
  }
  if (result != NET_OK) {
    flags = cpu_save_interrupts();
    net_packet_release(packet);
    cpu_restore_interrupts(flags);
  }
  return result;
}

void net_udp_receive_packet(uint32_t source, uint32_t destination,
    const uint8_t *message, size_t length)
{
  ++udp_wire_stats.received;
  if (length < UDP_HEADER_SIZE) {
    ++udp_wire_stats.malformed;
    return;
  }
  const struct udp_header *header = (const void *)message;
  size_t udp_length = net_read_u16(header->length);
  if (udp_length < UDP_HEADER_SIZE || udp_length > length ||
      udp_length > UDP_HEADER_SIZE + UDP_MAX_PAYLOAD) {
    ++udp_wire_stats.malformed;
    return;
  }
  /* IPv4 permits an omitted checksum. Otherwise check only the declared UDP
   * length, never link padding or bytes beyond this datagram. */
  if (net_read_u16(header->checksum) && udp_checksum(source, destination, message, udp_length)) {
    ++udp_wire_stats.bad_checksum;
    return;
  }
  net_udp_deliver(source, destination, net_read_u16(header->destination), message, udp_length);
}
