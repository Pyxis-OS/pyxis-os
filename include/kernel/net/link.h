#ifndef KERNEL_NET_LINK_H
#define KERNEL_NET_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* Rates are observed link bits per second, never advertised or bus maxima.
 * Zero is unknown; preference requires both directions while carrier is up. */
struct net_link_state {
  bool carrier_known, link_up;
  uint64_t tx_bits_per_second, rx_bits_per_second;
};

#endif
