#ifndef KERNEL_NET_DRIVER_H
#define KERNEL_NET_DRIVER_H

#include <abi/net_config.h>
#include <abi/syscall.h>
#include <kernel/net/interface.h>

enum call_status net_driver_resolve(const struct net_interface *iface,
    const struct net_selector *selector, struct net_driver_binding *controller);
bool net_driver_same_controller(struct net_driver_binding a, struct net_driver_binding b);
void net_driver_activate(const struct net_interface *iface);
void net_driver_panic_select(const struct net_interface *iface);
/* Fill only bound-controller identity, preparation/readiness and fresh carrier. */
void net_driver_interface_snapshot(const struct net_interface *iface,
    struct net_interface_reply *reply);
/* Worker, IF=1. Existing supported-controller discovery observation. */
bool net_driver_inventory_complete(void);
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
const struct net_interface *net_driver_panic_selected(void);
bool net_driver_panic_begin(const struct net_interface *iface, uint8_t mac[6]);
bool net_driver_panic_transmit(const struct net_interface *iface,
    const void *frame, size_t length);

#endif
