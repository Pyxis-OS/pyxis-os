#ifndef KERNEL_NET_DRIVER_H
#define KERNEL_NET_DRIVER_H

#include <abi/net_config.h>
#include <abi/syscall.h>
#include <kernel/net/interface.h>

/* Sole BSP network worker, IF=1. Unique match against retained controllers;
 * lookup is read-only. The first binding lasts until reboot. Failed matching
 * preserves it; another controller returns BUSY. No fallback after failure.
 * The interface and committed binding remain until reboot. */
enum call_status net_driver_bind(struct net_interface *iface,
    const struct net_selector *selector);
enum call_status net_driver_lookup(const struct net_interface *iface,
    const struct net_selector *selector,
    struct net_config_reply *reply);
void net_driver_snapshot(const struct net_interface *iface,
    struct net_config_reply *reply);
/* Fill only bound-controller identity, preparation/readiness and fresh carrier. */
void net_driver_interface_snapshot(const struct net_interface *iface,
    struct net_interface_reply *reply);
/* Read-only, fresh carrier snapshot in ascending opaque boot-local ID order. */
void net_driver_next_controller(uint32_t after_id,
    struct net_controller_reply *reply);

/* Worker, IF=1, including unavailable/stopping transports. Service keeps the
 * driver's existing bounded batch/yield semantics. Deadline also accepts IF=0.
 * Unbound operation is idle. The driver retains all hardware state/ownership. */
bool net_driver_service(const struct net_interface *iface);
bool net_driver_next_deadline(const struct net_interface *iface,
    uint64_t *deadline);

/* Worker snapshots; borrowed MAC only for this call. Availability includes
 * carrier and stable configuration. TX copies a complete Ethernet frame;
 * every result preserves caller storage, OK means queued, not delivered. */
const uint8_t *net_driver_mac(const struct net_interface *iface);
bool net_driver_available(const struct net_interface *iface);
enum net_result net_driver_transmit(const struct net_interface *iface,
    const void *frame, size_t length);

/* First fatal CPU, IF=0. Irreversible selected-driver handoff, bounded and
 * lock/allocation free. Failure retains DMA ownership and disables normal use.
 * A successful send means checked TX completion, never guaranteed delivery. */
bool net_driver_panic_begin(uint8_t mac[6]);
bool net_driver_panic_transmit(const void *frame, size_t length);

#endif
