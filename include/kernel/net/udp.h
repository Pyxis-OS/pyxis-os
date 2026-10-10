#ifndef KERNEL_NET_UDP_H
#define KERNEL_NET_UDP_H

#include <abi/udp.h>
#include <abi/syscall.h>
#include <kernel/net/interface.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>

/* Current user task/table, IF=0, no prepared BSP request. OPEN reserves a private
 * slot before parking. The worker owns the prepared grant and binding; successful
 * collection moves that grant into the slot. Failure releases private ownership.
 * No caller stack or user pointers are retained. */
enum call_status net_udp_open(struct capability_table *table, uint32_t address,
    uint16_t port, struct udp_open_reply *reply);
/* Select and bind on the worker, serialized with address/route changes. */
enum call_status net_udp_open_route(struct capability_table *table, uint32_t destination,
    uint16_t port, struct udp_open_reply *reply);
/* Bind wildcard net0, independent of its current IPv4 assignment. */
enum call_status net_udp_open_broadcast(struct capability_table *table, uint16_t port,
    struct udp_open_reply *reply);
enum call_status net_udp_inspect(struct kernel_object *object, struct udp_endpoint_info *reply);
enum call_status net_udp_shutdown(struct kernel_object *object);

/* User task, IF=0. Copy payloads through bounded shared slots before parking;
 * neither data nor reply pointers are lent to the worker. */
enum call_status net_udp_send(struct kernel_object *object, uint32_t address,
    uint16_t port, const uint8_t *data, size_t length, uint64_t deadline);
enum call_status net_udp_receive(struct kernel_object *object, size_t capacity,
    uint64_t deadline, uint8_t *data, struct udp_receive_reply *reply);

/* Worker, IF=1. Borrow RX bytes only for this call. Completion tokens identify
 * send requests, never endpoint addresses or reusable slot indices. */
void net_udp_receive_packet(uint32_t source, uint32_t destination,
    const uint8_t *message, size_t length);
void net_udp_transmitted(uint64_t token);
void net_udp_failed(uint64_t token, enum net_result result);
/* Worker, IF=0 or IF=1, like the existing echo deadline query. */
bool net_udp_next_deadline(uint64_t *deadline);

/* Sole network worker, IF=1. Service drains final-close retirement and eight
 * bounded control slots. Invalidation releases bindings for the removed address
 * without reviving them on reconfiguration. */
bool net_udp_service(void);
void net_udp_invalidate_address(uint32_t address);

#endif
