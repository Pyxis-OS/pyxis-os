#ifndef RTL8111_COUNTERS_H
#define RTL8111_COUNTERS_H

#include <stddef.h>
#include <stdint.h>

/* RTL8168H's little-endian tally DMA payload. */
struct rtl_counters {
  uint64_t tx_packets, rx_packets, tx_errors;
  uint32_t rx_errors;
  uint16_t rx_missed, align_errors;
  uint32_t tx_single_collision, tx_multi_collision;
  uint64_t rx_unicast, rx_broadcast;
  uint32_t rx_multicast;
  uint16_t tx_aborted, tx_underrun;
};

static_assert(sizeof(struct rtl_counters) == 64);
static_assert(offsetof(struct rtl_counters, rx_missed) == 28);

#endif
