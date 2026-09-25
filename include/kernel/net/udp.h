#ifndef KERNEL_NET_UDP_H
#define KERNEL_NET_UDP_H

#include <abi/udp.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>

/* User task, IF=0. OPEN lends its kernel-owned table to the BSP worker until
 * return; the parked task keeps its process, grants and mappings alive. The
 * worker installs the handle before publishing the binding. Failure leaves
 * no endpoint/binding behind. No caller stack or user pointers are retained. */
enum call_status net_udp_open(struct capability_table *table, uint32_t address,
    uint16_t port, struct udp_open_reply *reply);
enum call_status net_udp_inspect(struct kernel_object *object, struct udp_endpoint_info *reply);
enum call_status net_udp_shutdown(struct kernel_object *object);

/* Sole network worker, IF=1. Service drains final-close retirement and eight
 * bounded control slots. Invalidation releases bindings for the removed address
 * without reviving them on reconfiguration. */
bool net_udp_service(void);
void net_udp_invalidate_address(uint32_t address);

#endif
