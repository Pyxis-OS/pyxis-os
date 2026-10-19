#ifndef KERNEL_NET_CONFIG_H
#define KERNEL_NET_CONFIG_H

#include <abi/net_config.h>
#include <kernel/net/interface.h>

struct net_config_entry {
  struct net_interface_definition definition;
  struct net_interface_binding binding;
  struct net_interface *interface;
  struct net_driver_binding controller;
  bool published, activate;
};

/* BSP prepares and reclaims backing. Only the worker accesses queued payloads.
 * Interfaces/projections transferred into the registry remain until reboot. */
struct net_control {
  uint64_t protocol, operation, after_id, rights;
  struct net_interface *interface;
  size_t count;
  struct net_config_entry *entries;
  const struct net_interface **interfaces;
  struct net_config_request replace;
  struct net_clear_request clear;
  struct net_dhcp_report_request report;
  struct net_policy_request policy;
  struct net_interface_reply interface_reply;
  struct net_controller_reply controller_reply;
  struct net_policy_reply policy_reply;
};

bool net_control_valid_server(uint32_t address);
enum call_status net_config_exchange(struct net_control *control);
bool net_config_service(void);
void net_interface_snapshot(const struct net_interface *interface,
    struct net_interface_reply *reply);

#endif
