#ifndef CAELUM_TCP_IDENTITY_H
#define CAELUM_TCP_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

/* Worker, IF=1. Readiness is published once by the boot preparation task.
 * No waiting, device reads or predictable fallback from these operations. */
bool tcp_identity_ready(void);
bool tcp_identity_ports(uint32_t local, uint32_t remote, uint16_t remote_port,
    uint16_t *first, uint16_t *stride);

#define TCP_EPHEMERAL_FIRST 49152
#define TCP_EPHEMERAL_COUNT 16384

#endif
