#ifndef KERNEL_NET_INTERFACE_H
#define KERNEL_NET_INTERFACE_H

#include <kernel/mm/types.h>
#include <kernel/net/packet.h>
#include <kernel/net/controller.h>

#define NET_RECEIVE_QUEUE_LIMIT 16

struct netif;

/* Contexts and controller bindings live until reboot. Only the sole BSP network
 * worker changes L3 state and projections; published metadata may be borrowed.
 * Task 2 retains the two existing contexts, with no new public configuration. */
struct net_interface {
  const char *name;
  size_t mtu;
  struct net_driver_binding driver;
  struct {
    uint32_t address, mask, gateway;
    unsigned prefix;
    uint64_t address_epoch, route_revision;
  } ipv4;
  struct netif *tcp_projection;
  bool arp_link_available;
};

/* IP-only local delivery exists independently of a physical controller. */
extern struct net_interface net_loopback;

enum net_result {
  NET_OK,
  NET_INVALID,
  NET_UNAVAILABLE,
  NET_QUEUE_FULL,
  NET_NO_ROUTE,
  NET_NO_MEMORY,
  NET_TIMED_OUT,
};

/* Once on BSP/IF=0 after task_init(), before scheduling starts. Failure leaves
 * transmission unavailable; no packets are allocated or submitted at boot. */
enum mm_result net_init(void);

/* Ready is published once after worker creation. Notify is any CPU, IF=0;
 * publish work before notifying, without holding the protocol's own lock. */
bool net_worker_available(void);
void net_worker_notify(void);
/* Sole BSP worker with IF=1, outside interrupt/fault entry. */
void net_worker_assert_context(void);
/* Worker, IF=1. Discard deferred local TCP copies owned by this generation. */
void net_loopback_cancel_tcp(uint64_t generation);

/* BSP task/initialization context, IF=0. Deferred local delivery on the supplied logical interface.
 * Success transfers packet ownership to deferred receive; it means queued,
 * not protocol acceptance or delivery to an application. Every error preserves
 * caller ownership. No inline receive, allocation or waiting.
 * The worker validates IPv4 and dispatches supported protocols. */
enum net_result net_transmit(const struct net_interface *interface,
    struct net_packet *packet);

#endif
