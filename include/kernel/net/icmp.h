#ifndef KERNEL_NET_ICMP_H
#define KERNEL_NET_ICMP_H

#include <kernel/net/ipv4.h>

#define ICMP_ECHO_HEADER_SIZE 8
#define ICMP_ECHO_MAX_PAYLOAD (NET_PACKET_MAX_BYTES - IPV4_HEADER_SIZE - ICMP_ECHO_HEADER_SIZE)

/* BSP task/initialization context, IF=0. Copies payload into an owned packet
 * and queues an echo from 127.0.0.1. No retained caller storage. NET_OK means
 * queued, not answered. Reply matching and application waits come separately. */
enum net_result net_icmp_echo_send(uint32_t destination, uint16_t identifier,
    uint16_t sequence, const void *payload, size_t length);

/* Sole BSP network worker, IF=1. Borrows validated IPv4 payload for this call.
 * Checks ICMP before replying; received replies are counted and consumed until
 * the application echo facility supplies matching and completion. */
void net_icmp_receive(uint32_t source, uint32_t destination,
    const uint8_t *message, size_t length);

#endif
