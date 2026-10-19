#include <kernel/net/factory.h>
#include <kernel/panic.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/registry.h>
#include <kernel/net/driver.h>

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

static bool usable(const struct net_interface *interface, bool gateway)
{
  if (!interface || interface == &net_loopback || !interface->ipv4.address ||
      (gateway && !interface->ipv4.gateway) || !net_driver_available(interface)) {
    return false;
  }
  if (interface->dhcp_report_valid) {
    switch (interface->dhcp_phase) {
    case NET_DHCP_WAIT_LINK:
    case NET_DHCP_SELECTING:
    case NET_DHCP_EXPIRED:
    case NET_DHCP_FAILED:
      return false;
    default: break;
    }
  }
  return true;
}

static uint64_t observed_rate(const struct net_interface *interface)
{
  struct net_link_state link;
  net_driver_link_state(interface, &link);
  return link.tx_bits_per_second < link.rx_bits_per_second ?
      link.tx_bits_per_second : link.rx_bits_per_second;
}

static struct net_preference_member *preference[NET_PREFERENCE_COUNT];

static bool preferred(const struct net_preference_member *candidate,
    const struct net_preference_member *selected)
{
  if (candidate->usable != selected->usable) {
    return candidate->usable;
  }
  if (!candidate->usable) {
    return false;
  }
  if (candidate->interface->interface_class != selected->interface->interface_class) {
    return candidate->interface->interface_class == NET_INTERFACE_CLASS_ETHERNET;
  }
  return candidate->rate > selected->rate;
}

void net_route_refresh(void)
{
  net_worker_assert_context();
  if (!net_policy_automatic()) {
    return;
  }
  for (struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if (interface == &net_loopback) {
      continue;
    }
    bool available = usable(interface, false);
    uint64_t rate = available ? observed_rate(interface) : 0;
    for (unsigned kind = 0; kind < NET_PREFERENCE_COUNT; ++kind) {
      struct net_preference_member *member = &interface->preference[kind];
      bool add = !member->interface;
      if (!add && kind == NET_PREFERENCE_CONNECTED &&
          member->connected_epoch != interface->connected_epoch) {
        struct net_preference_member **at = &preference[kind];
        while (*at != member) {
          at = &(*at)->next;
        }
        *at = member->next;
        add = true;
      }
      if (add) {
        struct net_preference_member **tail = &preference[kind];
        while (*tail) {
          tail = &(*tail)->next;
        }
        member->next = NULL;
        member->interface = interface;
        *tail = member;
      }
      member->connected_epoch = interface->connected_epoch;
      member->usable = available &&
          (kind == NET_PREFERENCE_CONNECTED || interface->ipv4.gateway);
      member->rate = rate;
    }
  }
  /* Stable orders project onto every held subset. A down member moves behind
   * usable peers; returning at an equal rate cannot jump ahead of those peers.
   * Connected-group changes append before sorting, while renewals keep order. */
  for (unsigned kind = 0; kind < NET_PREFERENCE_COUNT; ++kind) {
    struct net_preference_member *remaining = preference[kind], *sorted = NULL;
    while (remaining) {
      struct net_preference_member *member = remaining;
      remaining = member->next;
      struct net_preference_member **at = &sorted;
      while (*at && !preferred(member, *at)) {
        at = &(*at)->next;
      }
      member->next = *at;
      *at = member;
    }
    preference[kind] = sorted;
  }
}

const struct net_interface *net_route_preferred_default(const struct net_factory *factory)
{
  net_worker_assert_context();
  net_route_refresh();
  for (const struct net_preference_member *member = preference[NET_PREFERENCE_DEFAULT];
      member; member = member->next) {
    if (member->usable && (!factory || net_factory_allows(factory, member->interface))) {
      return member->interface;
    }
  }
  return NULL;
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

  bool automatic = !restriction && net_policy_automatic();
  if (automatic) {
    net_route_refresh();
  }
  const struct net_interface *selected = NULL;
  for (const struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if ((restriction && restriction != interface) ||
        (factory && !net_factory_allows(factory, interface)) ||
        (automatic && !usable(interface, false)) ||
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
  if (selected && automatic) {
    unsigned prefix = selected->ipv4.prefix;
    for (const struct net_preference_member *member = preference[NET_PREFERENCE_CONNECTED];
        member; member = member->next) {
      const struct net_interface *interface = member->interface;
      if (member->usable && interface->ipv4.prefix == prefix &&
          (!factory || net_factory_allows(factory, interface)) &&
          (destination & interface->ipv4.mask) == (interface->ipv4.address & interface->ipv4.mask)) {
        selected = interface;
        break;
      }
    }
  }
  if (!selected) {
    selected = restriction ? restriction : automatic ?
        net_route_preferred_default(factory) : net_policy_default_interface();
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
