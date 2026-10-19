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

static bool preferred(const struct net_interface *candidate,
    const struct net_interface *selected)
{
  if (!selected) {
    return true;
  }
  if (candidate->interface_class != selected->interface_class) {
    return candidate->interface_class == NET_INTERFACE_CLASS_ETHERNET;
  }
  /* Unknown (zero) ranks after any observed pair. Strict improvement keeps
   * the remembered winner when class and effective rate are equal. */
  return observed_rate(candidate) > observed_rate(selected);
}

static const struct net_interface *default_winner;

const struct net_interface *net_route_preferred_default(const struct net_factory *factory)
{
  net_worker_assert_context();
  const struct net_interface *selected = factory ?
      factory->route_choices[0].selected : default_winner;
  if (!usable(selected, true) || (factory && !net_factory_allows(factory, selected))) {
    selected = NULL;
  }
  for (struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if ((!factory || net_factory_allows(factory, interface)) &&
        usable(interface, true) && preferred(interface, selected)) {
      selected = interface;
    }
  }
  if (factory) {
    factory->route_choices[0].selected = selected;
  } else {
    default_winner = selected;
  }
  return selected;
}

static struct net_route_choice *connected_choice(const struct net_factory *factory,
    struct net_interface *interface)
{
  if (!factory) {
    return &interface->connected_choice;
  }
  for (size_t i = 0; i < factory->interface_count; ++i) {
    if (factory->interfaces[i] == interface) {
      return &factory->route_choices[i + 1];
    }
  }
  return NULL;
}

static const struct net_interface *connected_winner(const struct net_factory *factory,
    uint32_t network, uint32_t mask)
{
  for (struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if (interface->ipv4.mask != mask || (interface->ipv4.address & mask) != network) {
      continue;
    }
    struct net_route_choice *choice = connected_choice(factory, interface);
    if (choice && choice->member_epoch == interface->connected_epoch &&
        choice->mask == mask && choice->network == network &&
        usable(choice->selected, false) && choice->selected->ipv4.mask == mask &&
        (choice->selected->ipv4.address & mask) == network &&
        (!factory || net_factory_allows(factory, choice->selected))) {
      return choice->selected;
    }
  }
  return NULL;
}

static void remember_connected(const struct net_factory *factory,
    const struct net_interface *selected)
{
  uint32_t mask = selected->ipv4.mask, network = selected->ipv4.address & mask;
  for (struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if (interface->ipv4.mask != mask || (interface->ipv4.address & mask) != network) {
      continue;
    }
    struct net_route_choice *choice = connected_choice(factory, interface);
    if (choice) {
      *choice = (struct net_route_choice){.selected = selected, .network = network, .mask = mask,
        .member_epoch = interface->connected_epoch};
    }
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

  bool automatic = !restriction && net_policy_automatic();
  const struct net_interface *selected = NULL;
  for (struct net_interface *interface = net_interface_first(); interface;
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
    uint32_t mask = selected->ipv4.mask, network = selected->ipv4.address & mask;
    const struct net_interface *remembered = connected_winner(factory, network, mask);
    if (remembered) {
      selected = remembered;
    }
    for (struct net_interface *interface = net_interface_first(); interface;
        interface = interface->registry_next) {
      if ((!factory || net_factory_allows(factory, interface)) &&
          interface->ipv4.mask == mask && (interface->ipv4.address & mask) == network &&
          usable(interface, false) && preferred(interface, selected)) {
        selected = interface;
      }
    }
    remember_connected(factory, selected);
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
