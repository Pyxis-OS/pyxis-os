#ifndef KERNEL_VIRTIO_NET_H
#define KERNEL_VIRTIO_NET_H

#include <kernel/boot.h>
#include <kernel/net/interface.h>

/* Once on BSP/IF=0 before AP startup. Prepare PCI resources and owned RX/TX
 * storage. DRIVER_OK, DMA and MSI-X delivery stay disabled until worker start.
 * Failed boot preparation releases storage only after confirmed device reset. */
void virtio_net_prepare(const struct boot_info *boot);

/* Sole BSP network worker, IF=1. Start once; service at most one batch of sixteen
 * RX and sixteen TX completions per turn. True requests a yield before another
 * batch. Stop/reset is serviced incrementally without blocking loopback work.
 * Failed runtime devices retain all storage and mappings until reboot. */
void virtio_net_start(void);
bool virtio_net_service(void);
/* Same worker, IF=0 or IF=1. Earliest outstanding TX/config/reset deadline;
 * false when idle. Idle RX has no deadline and does not poll. */
bool virtio_net_next_deadline(uint64_t *deadline);

/* BSP interrupt entry, IF=0: wake worker only; arch owns APIC acknowledgement. */
void virtio_net_interrupt(void);

/* Sole BSP network worker, IF=1. Copy a complete checksummed Ethernet frame
 * (14..1514 bytes, no FCS) into driver-owned storage. All returns leave caller
 * storage owned by the caller; NET_OK means queued, not delivered. A full queue
 * returns NET_QUEUE_FULL. No user pointers, allocation, waiting or cancellation. */
enum net_result virtio_net_transmit(const void *frame, size_t length);

/* Worker-only snapshots. The MAC is immutable while prepared/active and may
 * be borrowed for the call; NULL means no usable device. Availability includes
 * link state and stable configuration, unlike MAC/configuration presence. */
const uint8_t *virtio_net_mac(void);
bool virtio_net_available(void);

#endif
