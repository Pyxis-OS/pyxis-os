#ifndef KERNEL_NET_UDP_H
#define KERNEL_NET_UDP_H

#include <abi/udp.h>
#include <abi/syscall.h>
#include <kernel/net/interface.h>
#include <kernel/net/factory.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>

/* Current user task/table, IF=0, no prepared BSP request. OPEN reserves a private
 * slot before parking. The worker owns the prepared grant and binding; successful
 * collection moves that grant into the slot. Failure releases private ownership.
 * The owned charged request stays joined until worker completion.
 * No caller stack or user pointers are retained. */
enum call_status net_udp_open(struct net_factory *factory,
    struct capability_table *table, struct net_path path, uint32_t address,
    uint16_t port, struct udp_open_reply *reply);
/* Select and bind on the worker, serialized with address/route changes. */
enum call_status net_udp_open_route(struct net_factory *factory,
    struct capability_table *table, struct net_path path, uint32_t destination,
    uint16_t port, struct udp_open_reply *reply);
/* Bind wildcard on one explicitly selected scoped interface, independent of its address. */
enum call_status net_udp_open_broadcast(struct net_factory *factory,
    struct capability_table *table, struct net_path path, uint16_t port,
    struct udp_open_reply *reply);
enum call_status net_udp_inspect(struct kernel_object *object, struct udp_endpoint_info *reply);
enum call_status net_udp_shutdown(struct kernel_object *object);

/* User task, IF=0. Copy payloads into owned charged requests before parking;
 * neither data nor reply pointers are lent to the worker. */
enum call_status net_udp_send(struct kernel_object *object, uint32_t address,
    uint16_t port, const uint8_t *data, size_t length, uint64_t deadline);
enum call_status net_udp_receive(struct kernel_object *object, size_t capacity,
    uint64_t deadline, uint8_t *data, struct udp_receive_reply *reply);

struct native_completion;
/* BSP/IF=0. Reserve direction and owned request before acceptance; no I/O wait. */
enum call_status net_udp_completion_reserve(struct native_completion *operation);
void net_udp_completion_start(struct native_completion *operation);
void net_udp_completion_discard(struct native_completion *operation);

/* Worker, IF=1. Borrow RX bytes only for this call. Completion tokens identify
 * send requests, never endpoint addresses or reusable storage identities. */
void net_udp_receive_packet(const struct net_interface *interface,
    uint32_t source, uint32_t destination, const uint8_t *message, size_t length);
void net_udp_transmitted(uint64_t token);
void net_udp_failed(uint64_t token, enum net_result result);
/* Any CPU, no subsystem lock: preserve changes across a partial worker sweep. */
void net_udp_changed(void);
/* Worker, IF=0 or IF=1, like the existing echo deadline query. */
bool net_udp_next_deadline(uint64_t *deadline);

/* Sole network worker, IF=1. Bounded turns drain controls, I/O and retirement.
 * Epoch checks retire concrete authority immediately; queued storage cleanup
 * resumes on worker turns. Broadcast bindings survive address replacement. */
bool net_udp_service(void);
void net_udp_invalidate_address(const struct net_interface *interface, uint64_t retired_epoch);

#endif
