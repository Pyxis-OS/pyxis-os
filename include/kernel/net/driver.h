#ifndef KERNEL_NET_DRIVER_H
#define KERNEL_NET_DRIVER_H

#include <abi/net_config.h>
#include <abi/syscall.h>
#include <kernel/net/interface.h>

/* Sole BSP network worker, IF=1. Unique match against retained controllers;
 * lookup is read-only. The first binding lasts until reboot. Failed matching
 * preserves it; another controller returns BUSY. No fallback after failure. */
enum call_status net_driver_bind(const struct net_selector *selector);
enum call_status net_driver_lookup(const struct net_selector *selector,
    struct net_config_reply *reply);
void net_driver_snapshot(struct net_config_reply *reply);

/* Worker, IF=1, including unavailable/stopping transports. Service keeps the
 * driver's existing bounded batch/yield semantics. Deadline also accepts IF=0.
 * Unbound operation is idle. The driver retains all hardware state/ownership. */
bool net_driver_service(void);
bool net_driver_next_deadline(uint64_t *deadline);

/* Worker snapshots; borrowed MAC only for this call. Availability includes
 * carrier and stable configuration. TX copies a complete Ethernet frame;
 * every result preserves caller storage, OK means queued, not delivered. */
const uint8_t *net_driver_mac(void);
bool net_driver_available(void);
enum net_result net_driver_transmit(const void *frame, size_t length);

#endif
