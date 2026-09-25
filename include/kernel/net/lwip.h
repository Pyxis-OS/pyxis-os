#ifndef KERNEL_NET_LWIP_H
#define KERNEL_NET_LWIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sole BSP network worker, IF=1. Initialize once before processing packets.
 * Only validated, locally addressed TCP datagrams reach receive; bytes are
 * borrowed for the call and copied before entering lwIP. No inline delivery
 * of output back into input, even for loopback. */
void net_lwip_init(void);
void net_lwip_receive(const uint8_t *data, size_t length);
void net_lwip_refresh_address(void);
void net_lwip_service(void);
/* Relative lwIP milliseconds become a saturating absolute monotonic deadline.
 * False means idle; the worker need not wake periodically for lwIP. */
bool net_lwip_next_deadline(uint64_t *deadline);

#endif
