#include <kernel/net/factory.h>
#include <kernel/panic.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/registry.h>

static enum call_status route_status(enum net_result result)
{
  switch (result) {
  case NET_OK: return CALL_OK;
  case NET_INVALID: return CALL_BAD_REQUEST;
  case NET_UNAVAILABLE: return CALL_UNAVAILABLE;
  case NET_NO_ROUTE: return CALL_NO_ROUTE;
  default: return CALL_BAD_REQUEST;
  }
}

static enum call_status select_route(const struct net_factory *factory,
    const struct net_interface *pinned, uint32_t source, uint32_t destination,
    struct ipv4_route *route)
{
  net_worker_assert_context();
  const struct net_interface *source_owner = source ?
      net_interface_for_address(source) : NULL;
  if (source && !source_owner) {
    return CALL_BAD_REQUEST;
  }
  if (pinned && source_owner && pinned != source_owner) {
    return CALL_BAD_REQUEST;
  }
  if ((pinned && factory && !net_factory_allows(factory, pinned)) ||
      (source_owner && factory && !net_factory_allows(factory, source_owner))) {
    return CALL_DENIED;
  }
  const struct net_interface *restriction = pinned ? pinned : source_owner;
  if (!restriction && factory && factory->interface_count == 1) {
    /* The capability itself pins this route, including its off-link gateway. */
    restriction = factory->interfaces[0];
  }
  const struct net_interface *local = net_interface_for_address(destination);
  if (local) {
    if (factory && !net_factory_allows(factory, local)) {
      return CALL_DENIED;
    }
    if (restriction && restriction != local) {
      return CALL_BAD_REQUEST;
    }
    return route_status(net_ipv4_route_on(local, source, destination, route));
  }
  if (!(destination >> 24) || destination >= UINT32_C(0xe0000000)) {
    return CALL_BAD_REQUEST;
  }

  const struct net_interface *selected = NULL;
  for (struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if ((restriction && restriction != interface) ||
        (factory && !net_factory_allows(factory, interface)) ||
        !interface->ipv4.address ||
        interface == &net_loopback) {
      continue;
    }
    if ((destination & interface->ipv4.mask) ==
        (interface->ipv4.address & interface->ipv4.mask) &&
        (!selected || interface->ipv4.prefix > selected->ipv4.prefix)) {
      selected = interface;
    }
  }
  if (!selected) {
    selected = restriction ? restriction : net_policy_default_interface();
    if (!selected || (factory && !net_factory_allows(factory, selected))) {
      return CALL_NO_ROUTE;
    }
  }
  return route_status(net_ipv4_route_on(selected, source, destination, route));
}

enum call_status net_factory_route(const struct net_factory *factory,
    const struct net_interface *pinned, uint32_t source, uint32_t destination,
    struct ipv4_route *route)
{
  KASSERT(factory);
  return select_route(factory, pinned, source, destination, route);
}

/* Internal TCP projection fallback shares the native route policy. Public
 * transport operations always supply the held factory to the entry above. */
enum net_result net_ipv4_route(uint32_t source, uint32_t destination,
    struct ipv4_route *route)
{
  switch (select_route(NULL, NULL, source, destination, route)) {
  case CALL_OK: return NET_OK;
  case CALL_UNAVAILABLE: return NET_UNAVAILABLE;
  case CALL_NO_ROUTE:
  case CALL_DENIED: return NET_NO_ROUTE;
  default: return NET_INVALID;
  }
}
