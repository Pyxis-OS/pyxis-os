#ifndef KERNEL_NET_ICMP_H
#define KERNEL_NET_ICMP_H

#include <kernel/net/ipv4.h>

#define ICMP_ECHO_HEADER_SIZE 8
#define ICMP_ECHO_MAX_PAYLOAD (NET_PACKET_MAX_BYTES - IPV4_HEADER_SIZE - ICMP_ECHO_HEADER_SIZE)

/* Sole BSP worker, IF=1. Copies payload into an owned packet, queued locally
 * or through ARP. NET_OK means queued, not answered; the echo service owns
 * application reply matching and its deadline includes resolution. */
enum net_result net_icmp_echo_send(uint32_t source, uint32_t destination,
    uint16_t identifier, uint16_t sequence, const void *payload, size_t length,
    uint64_t deadline, uint64_t token);

/* Sole BSP network worker, IF=1. Borrows validated IPv4 payload for this call.
 * Checks ICMP before replying or dispatching replies to the echo service. */
void net_icmp_receive(uint32_t source, uint32_t destination,
    const uint8_t *message, size_t length);

#endif
