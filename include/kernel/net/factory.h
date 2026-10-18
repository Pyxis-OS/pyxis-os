#ifndef KERNEL_NET_FACTORY_H
#define KERNEL_NET_FACTORY_H

#include <abi/syscall.h>
#include <kernel/net/interface.h>
#include <kernel/object/object.h>

struct ipv4_route;

/* Scope is immutable after publication. Interface contexts live until reboot;
 * the factory owns this copied vector of retained context identities. */
struct net_factory {
  struct kernel_object object;
  size_t interface_count;
  const struct net_interface *interfaces[];
};

/* BSP/IF=0. One owned reference, or NULL without publishing any authority. */
struct kernel_object *net_factory_create(enum object_type type,
    const struct net_interface *const *interfaces, size_t count);
bool net_factory_allows(const struct net_factory *factory,
    const struct net_interface *interface);

/* Sole network worker, IF=1. Apply held scope before routing or local delivery. */
enum call_status net_factory_route(const struct net_factory *factory,
    const struct net_interface *pinned, uint32_t source, uint32_t destination,
    struct ipv4_route *route);

#endif
