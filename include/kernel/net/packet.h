#ifndef KERNEL_NET_PACKET_H
#define KERNEL_NET_PACKET_H

#include <stddef.h>
#include <stdint.h>

/* Initial software packet budget, independent of device DMA buffers. */
#define NET_PACKET_MAX_BYTES 1500
#define NET_PACKET_LIMIT 32

struct net_packet {
  size_t length;
  uint8_t data[];
};

/* BSP task/initialization context, IF=0; never interrupt/fault entry.
 * Allocation returns one exclusively owned buffer, or NULL for zero/oversized
 * length, budget exhaustion or heap failure. Length is immutable after creation.
 * The caller initializes all data before publishing the packet. These are CPU
 * buffers, not DMA allocations, and contain no borrowed user/stack storage. */
struct net_packet *net_packet_allocate(size_t length);

/* Consumes one owned buffer. NULL is harmless. Never release a queued packet. */
void net_packet_release(struct net_packet *packet);

#endif
