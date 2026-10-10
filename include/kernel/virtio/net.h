#ifndef KERNEL_VIRTIO_NET_H
#define KERNEL_VIRTIO_NET_H

#include <kernel/boot.h>
#include <kernel/net/interface.h>
#include <kernel/net/debug.h>

struct virtio_net_controller;

/* Once on BSP/IF=0 before AP startup. Prepare PCI resources and owned RX/TX
 * storage for every modern VirtIO-net candidate. DRIVER_OK, DMA and MSI-X
 * delivery stay disabled until selected worker start. Failed boot preparation
 * releases storage only after confirmed device reset. */
void virtio_net_prepare(const struct boot_info *boot);

/* Retained candidate records live until reboot, including preparation failures.
 * Enumeration/identity reads no registers. A known MAC remains immutable even
 * after failure; NULL means identity unknown. Incomplete PCI inventory or a
 * candidate record allocation failure makes inventory_complete false. */
struct virtio_net_controller *virtio_net_first(void);
struct virtio_net_controller *virtio_net_next(const struct virtio_net_controller *controller);
const uint8_t *virtio_net_identity_mac(const struct virtio_net_controller *controller);
bool virtio_net_inventory_complete(void);
uint32_t virtio_net_controller_id(const struct virtio_net_controller *controller);
bool virtio_net_prepared(const struct virtio_net_controller *controller);
/* Worker-only, bounded read-only sampling. False means carrier is unknown,
 * including devices without STATUS. Does not change cached config or ownership. */
bool virtio_net_carrier(const struct virtio_net_controller *controller, bool *up);

/* Sole BSP network worker, IF=1. Start once; service at most one batch of sixteen
 * RX and sixteen TX completions per turn. True requests a yield before another
 * batch. Stop/reset is serviced incrementally without blocking loopback work.
 * Failed runtime devices retain all storage and mappings until reboot. */
void virtio_net_start(struct virtio_net_controller *controller);
bool virtio_net_service(struct virtio_net_controller *controller);
/* Same worker, IF=0 or IF=1. Earliest outstanding TX/config/reset deadline;
 * false when idle. Idle RX has no deadline and does not poll. */
bool virtio_net_next_deadline(struct virtio_net_controller *controller, uint64_t *deadline);

/* BSP interrupt entry, IF=0: wake worker only; arch owns APIC acknowledgement. */
void virtio_net_interrupt(void);

/* Sole BSP network worker, IF=1. Copy a complete checksummed Ethernet frame
 * (14..1514 bytes, no FCS) into driver-owned storage. All returns leave caller
 * storage owned by the caller; NET_OK means queued, not delivered. A full queue
 * returns NET_QUEUE_FULL. No user pointers, allocation, waiting or cancellation. */
enum net_result virtio_net_transmit(struct virtio_net_controller *controller,
    const void *frame, size_t length);

/* Worker-only snapshots. The MAC is immutable while prepared/active and may
 * be borrowed for the call; NULL means no usable device. Availability includes
 * link state and stable configuration, unlike MAC/configuration presence. */
const uint8_t *virtio_net_mac(const struct virtio_net_controller *controller);
bool virtio_net_available(const struct virtio_net_controller *controller);
/* Ready means active and stable, independent of carrier; no register reads. */
bool virtio_net_ready(const struct virtio_net_controller *controller);

/* First fatal CPU, IF=0. Begin revokes normal networking permanently; false
 * leaves it revoked. Transmit is bounded and reuses storage only after a checked
 * completion. No allocation, worker/protocol dependency or reset. */
bool virtio_net_panic_begin(struct virtio_net_controller *controller);
bool virtio_net_panic_transmit(struct virtio_net_controller *controller,
    const void *frame, size_t length);

/* Native debugger owner; same stopped/worker contracts as net/debug.h. */
bool virtio_net_debug_ready(struct virtio_net_controller *controller,
    struct net_debug_device *device);
bool virtio_net_debug_service(struct virtio_net_controller *controller);
enum net_debug_status virtio_net_debug_begin(struct virtio_net_controller *controller,
    uint64_t generation);
enum net_debug_status virtio_net_debug_poll(struct virtio_net_controller *controller,
    uint64_t generation, void *frame, size_t capacity, size_t *length);
enum net_debug_status virtio_net_debug_transmit(struct virtio_net_controller *controller,
    uint64_t generation, const void *frame, size_t length);
enum net_debug_status virtio_net_debug_restore(struct virtio_net_controller *controller,
    uint64_t generation);
bool virtio_net_debug_retained(const struct virtio_net_controller *controller);

#endif
