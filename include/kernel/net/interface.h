#ifndef KERNEL_NET_INTERFACE_H
#define KERNEL_NET_INTERFACE_H

#include <kernel/mm/types.h>
#include <kernel/net/packet.h>

#define NET_RECEIVE_QUEUE_LIMIT 16

/* Boot-lifetime metadata, immutable and safe to borrow after publication.
 * No address configuration, driver callbacks or per-space network domains yet. */
struct net_interface {
  const char *name;
  size_t mtu;
};

/* The sole current interface carries IP packets, without link-layer headers.
 * It exists independently of PCI. Addressing and IP processing come next. */
extern const struct net_interface net_loopback;

enum net_result {
  NET_OK,
  NET_INVALID,
  NET_UNAVAILABLE,
  NET_QUEUE_FULL,
};

/* Once on BSP/IF=0 after task_init(), before scheduling starts. Failure leaves
 * transmission unavailable; no packets are allocated or submitted at boot. */
enum mm_result net_init(void);

/* BSP task/initialization context, IF=0. Only net_loopback is supported.
 * Success transfers packet ownership to deferred receive; it means queued,
 * not protocol acceptance or delivery to an application. Every error preserves
 * caller ownership. No inline receive, allocation or waiting.
 * The worker currently counts/discards unsupported protocol input. */
enum net_result net_transmit(const struct net_interface *interface,
    struct net_packet *packet);

#endif
