#include <kernel/net/factory.h>
#include <kernel/net/ethernet.h>
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

enum call_status net_factory_route(const struct net_factory *factory,
    const struct net_interface *pinned, uint32_t source, uint32_t destination,
    struct ipv4_route *route)
{
  net_worker_assert_context();
  if (pinned && !net_factory_allows(factory, pinned)) {
    return CALL_DENIED;
  }
  const struct net_interface *source_owner = source ?
      net_interface_for_address(source) : NULL;
  if (source && !source_owner) {
    return CALL_BAD_REQUEST;
  }
  if (source_owner && !net_factory_allows(factory, source_owner)) {
    return CALL_DENIED;
  }
  if (pinned && source_owner && pinned != source_owner) {
    return CALL_BAD_REQUEST;
  }
  const struct net_interface *restriction = pinned ? pinned : source_owner;
  const struct net_interface *local = net_interface_for_address(destination);
  if (local) {
    if (!net_factory_allows(factory, local)) {
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
        !net_factory_allows(factory, interface) || !interface->ipv4.address ||
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
    /* The existing bootstrap's explicitly selected default remains net0 until
     * trusted policy setup replaces the public single-interface configuration. */
    selected = restriction ? restriction : &net_ethernet;
    if (!net_factory_allows(factory, selected)) {
      return CALL_NO_ROUTE;
    }
  }
  return route_status(net_ipv4_route_on(selected, source, destination, route));
}
