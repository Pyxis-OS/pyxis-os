#ifndef KERNEL_NET_IPV4_H
#define KERNEL_NET_IPV4_H

#include <stdbool.h>
#include <abi/net_config.h>
#include <kernel/net/interface.h>

#define IPV4_HEADER_SIZE 20
#define IPV4_PROTOCOL_ICMP 1
#define IPV4_PROTOCOL_TCP 6
#define IPV4_PROTOCOL_UDP 17
#define IPV4_LOOPBACK_ADDRESS UINT32_C(0x7f000001)

struct ipv4_route {
  uint32_t source;
  uint32_t next_hop; /* Zero means deferred local delivery. */
};

/* Worker, IF=1. Concrete consumers of local-queue/NIC acceptance, not DMA completion.
 * Zero with NONE or TCP denotes a stateless reply. Nonzero tokens are never
 * reused, so delayed ARP completion cannot target a later request. */
enum ipv4_notify { IPV4_NOTIFY_NONE, IPV4_NOTIFY_ECHO, IPV4_NOTIFY_UDP, IPV4_NOTIFY_TCP };
struct ipv4_completion {
  enum ipv4_notify consumer;
  uint64_t token;
};
void net_ipv4_transmitted(struct ipv4_completion completion);
void net_ipv4_failed(struct ipv4_completion completion, enum net_result result);

/* All addresses are host-order integers: 127.0.0.1 is 0x7f000001.
 * Configuration and routing belong to the sole BSP network worker, IF=1.
 * Replace validates the whole configuration before changing anything; prefixes
 * 1..32, one unicast address, zero or one distinct on-link gateway (zero = none).
 * Clear/replace discard ARP state and fail outstanding non-loopback echo calls.
 * Configuration survives link down/up; it does not configure the host backend. */
enum net_result net_ipv4_configure(uint32_t address, unsigned prefix, uint32_t gateway);
void net_ipv4_clear(void);
void net_ipv4_snapshot(struct net_config_reply *reply);
uint32_t net_ipv4_address(void);
bool net_ipv4_is_neighbor(uint32_t address);
bool net_ipv4_is_loopback(uint32_t address);
/* A zero source selects one; a supplied source must belong to the chosen route.
 * Loopback and the assigned NIC address are delivered locally even if link down. */
enum net_result net_ipv4_route(uint32_t source, uint32_t destination,
    struct ipv4_route *route);

/* Worker, IF=1. Packet reserves a header before initialized payload. Success
 * consumes it; errors retain ownership but may have written the header.
 * Deadline includes ARP wait; completion names the concrete waiting consumer. */
enum net_result net_ipv4_transmit(struct net_packet *packet, uint32_t source,
    uint32_t destination, uint8_t protocol, uint64_t deadline, struct ipv4_completion completion);

/* Worker, IF=1. Submit an already complete IPv4 datagram without rewriting
 * headers. Validates its shape/checksum and rechecks source/route authority.
 * Success consumes the packet; failure leaves it unchanged and caller-owned. */
enum net_result net_ipv4_submit(struct net_packet *packet, uint64_t deadline,
    struct ipv4_completion completion);
/* Cancel unsent ARP and local-queue copies; DMA submissions cannot be recalled. */
void net_ipv4_cancel_tcp(uint64_t generation);

/* Worker, IF=1. Borrows bytes only for the call, including DMA-backed RX bytes. */
void net_ipv4_receive(const struct net_interface *interface,
    const uint8_t *data, size_t length);

#endif
