#ifndef KERNEL_NET_ETHERNET_H
#define KERNEL_NET_ETHERNET_H

#include <kernel/net/interface.h>

#define ETHERNET_ADDRESS_BYTES 6
#define ETHERNET_HEADER_BYTES 14
#define ETHERNET_FRAME_MAX (ETHERNET_HEADER_BYTES + NET_PACKET_MAX_BYTES)
#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP 0x0806

extern const struct net_interface net_ethernet;
extern const uint8_t net_ethernet_broadcast[ETHERNET_ADDRESS_BYTES];

bool net_ethernet_is_unicast(const uint8_t address[ETHERNET_ADDRESS_BYTES]);

/* Sole BSP network worker, IF=1. Both calls borrow storage only for the call.
 * Transmit copies into driver-owned DMA storage; it never consumes payload. */
enum net_result net_ethernet_transmit(const uint8_t destination[ETHERNET_ADDRESS_BYTES],
    uint16_t type, const void *payload, size_t length);
void net_ethernet_receive(const uint8_t *frame, size_t length);

#endif
