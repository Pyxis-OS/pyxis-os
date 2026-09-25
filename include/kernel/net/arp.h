#ifndef KERNEL_NET_ARP_H
#define KERNEL_NET_ARP_H

#include <kernel/net/ethernet.h>
#include <kernel/net/ipv4.h>

/* Sole BSP network worker, IF=1. Success consumes the packet, either copying it
 * to DMA storage or queuing it until resolution. Errors retain caller ownership.
 * The deadline includes resolution; completion identifies the waiting consumer. */
enum net_result net_arp_transmit(struct net_packet *packet, uint32_t next_hop,
    uint64_t deadline, struct ipv4_completion completion);
/* Worker, IF=1. Discard an unsent packet without notifying its consumer, which
 * owns cancellation/completion ordering. Submitted DMA cannot be recalled. */
void net_arp_cancel(struct ipv4_completion completion);
void net_arp_receive(const uint8_t source[ETHERNET_ADDRESS_BYTES],
    const uint8_t *message, size_t length);
void net_arp_service(void);
void net_arp_clear(enum net_result reason);
/* Same worker, IF=0 or IF=1. No periodic wakeup for an idle cache. */
bool net_arp_next_deadline(uint64_t *deadline);

#endif
