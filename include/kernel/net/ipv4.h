#ifndef KERNEL_NET_IPV4_H
#define KERNEL_NET_IPV4_H

#include <stdbool.h>
#include <kernel/net/interface.h>

#define IPV4_HEADER_SIZE 20
#define IPV4_PROTOCOL_ICMP 1
#define IPV4_LOOPBACK_ADDRESS UINT32_C(0x7f000001)

/* Addresses are host-order integers: 127.0.0.1 is 0x7f000001. Only the
 * loopback /8 is routed yet; it must never fall through to an external link. */
bool net_ipv4_is_loopback(uint32_t address);

/* BSP task/initialization context, IF=0. Packet includes IPV4_HEADER_SIZE
 * reserved bytes followed by an initialized payload. Fills the header and
 * queues local delivery. Success transfers ownership; failure retains it,
 * but may have written the header. Both addresses must currently be loopback. */
enum net_result net_ipv4_transmit(struct net_packet *packet, uint32_t source,
    uint32_t destination, uint8_t protocol);

/* Sole BSP network worker, IF=1. Borrows the packet only for this call;
 * validates and dispatches it without taking ownership. */
void net_ipv4_receive(const struct net_packet *packet);

#endif
