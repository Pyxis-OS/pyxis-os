#ifndef CAELUM_LWIP_HOOKS_H
#define CAELUM_LWIP_HOOKS_H

#include <lwip/ip4_addr.h>
#include <lwip/err.h>

struct netif;
struct pbuf;
struct tcp_pcb;

struct netif *caelum_lwip_route(const ip4_addr_t *source, const ip4_addr_t *destination);
int caelum_lwip_accept(struct netif *interface, const ip4_addr_t *destination);
err_t caelum_lwip_pcb_allocated(struct tcp_pcb *pcb);
err_t caelum_lwip_output(const struct tcp_pcb *pcb, struct pbuf *packet,
    const ip4_addr_t *source, const ip4_addr_t *destination,
    u8_t ttl, u8_t tos, struct netif *interface);
u32_t caelum_lwip_isn(const ip4_addr_t *local, u16_t local_port,
    const ip4_addr_t *remote, u16_t remote_port);

#endif
