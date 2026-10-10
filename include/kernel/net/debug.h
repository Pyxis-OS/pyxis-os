#ifndef KERNEL_NET_DEBUG_H
#define KERNEL_NET_DEBUG_H

#include <kernel/pci.h>

enum net_debug_status {
  NET_DEBUG_OK,
  NET_DEBUG_IDLE,
  NET_DEBUG_PENDING,
  NET_DEBUG_UNAVAILABLE,
  NET_DEBUG_UNSAFE,
  NET_DEBUG_FAILED,
};

struct net_debug_range {
  phys_addr_t physical;
  uint64_t bytes;
};

struct net_debug_device {
  uint32_t controller_id;
  uint8_t mac[6];
  struct pci_address pci;
  struct net_debug_range bars[PCI_BAR_COUNT];
  struct net_debug_range dma[3];
  unsigned dma_count;
};

/* Boot BSP, IF=0, before AP startup. Existing rings provide all DMA storage. */
void net_driver_debug_enable(void);
/* Enabled BSP worker, IF=1. Query readiness outside protocol/driver work.
 * Device metadata is immutable; IPv4 remains net0's authority. */
bool net_driver_debug_ready(struct net_debug_device *device);
bool net_driver_debug_service(void);

/* Parked BSP, IF=0; caller owns all-stop and supplies its nonzero generation.
 * UNSAFE/UNAVAILABLE before acquisition permit fatal fallback. FAILED after
 * acquisition retains exclusive ownership and storage until reboot.
 * Poll validates TX and copies at most one RX frame, reposting before return;
 * IDLE has no frame. No protocols, allocation, scheduler or logging.
 * Transmit OK means queued; caller storage stays its own. PENDING waits for
 * capacity or confirmed completion, never revoking DMA ownership.
 * Restore OK confirms all debug TX and mask restoration. Ordinary pending TX
 * retains its deadlines. Failure must not release CPUs. */
enum net_debug_status net_driver_debug_begin(uint64_t generation);
enum net_debug_status net_driver_debug_poll(uint64_t generation,
    void *frame, size_t capacity, size_t *length);
enum net_debug_status net_driver_debug_transmit(uint64_t generation,
    const void *frame, size_t length);
enum net_debug_status net_driver_debug_restore(uint64_t generation);
bool net_driver_debug_retained(void);

#endif
