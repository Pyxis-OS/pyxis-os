#ifndef KERNEL_NET_WIFI_H
#define KERNEL_NET_WIFI_H

#include <abi/net_config.h>
#include <kernel/net/interface.h>

/* Network worker, IF=1. Radio queries and frame copies are bounded BSP/IF=0
 * exchanges. Firmware commands, DMA and radio waits stay in the radio worker. */
void net_wifi_inventory(struct net_controller_reply *reply);
void net_wifi_interface_snapshot(const struct net_interface *interface,
    struct net_interface_reply *reply);
void net_wifi_start(const struct net_interface *interface);
bool net_wifi_service(struct net_interface *interface);
const uint8_t *net_wifi_mac(void);
bool net_wifi_available(const struct net_interface *interface);
enum net_result net_wifi_transmit(const struct net_interface *interface,
    const void *frame, size_t length);

/* Consume radio retirement only on the L3 owner. Clear address state even when
 * the latest radio incarnation is already closed or no address was assigned. */
bool net_wifi_sync(struct net_interface *interface);
bool net_wifi_generation_valid(const struct net_interface *interface,
    uint64_t generation, bool require_ready);

#endif
