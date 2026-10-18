#include <kernel/net/registry.h>
#include <kernel/net/ipv4.h>

struct net_interface *net_interface_first(void)
{
  return &net_loopback;
}

bool net_interface_registered(const struct net_interface *interface)
{
  for (struct net_interface *entry = net_interface_first(); entry;
      entry = entry->registry_next) {
    if (entry == interface) {
      return true;
    }
  }
  return false;
}

const struct net_interface *net_interface_for_address(uint32_t address)
{
  net_worker_assert_context();
  if (net_ipv4_is_loopback(address)) {
    return &net_loopback;
  }
  if (!address) {
    return NULL;
  }
  for (struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if (interface->ipv4.address == address) {
      return interface;
    }
  }
  return NULL;
}
