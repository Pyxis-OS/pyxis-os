#ifndef KERNEL_NET_REGISTRY_H
#define KERNEL_NET_REGISTRY_H

#include <kernel/net/interface.h>
#include <abi/net_config.h>

struct net_control;

/* Worker-owned links and contexts are retained until reboot. */
struct net_interface *net_interface_first(void);
bool net_interface_registered(const struct net_interface *interface);
const struct net_interface *net_interface_for_address(uint32_t address);
struct net_interface *net_interface_for_id(uint64_t id);
/* Worker-only validated publication. Prepared storage transfers on success. */
enum call_status net_registry_define(struct net_control *control);
enum call_status net_policy_replace(const struct net_policy_request *request);
void net_policy_snapshot(struct net_policy_reply *reply);
/* Worker-only current route policy; automatic selection observes live state. */
bool net_policy_automatic(void);
const struct net_interface *net_policy_default_interface(void);
/* Retained published management pointers; safe to borrow outside the worker. */
const struct net_interface *net_policy_remote_interface(void);
const struct net_interface *net_policy_log_interface(void);
const struct net_interface *net_policy_debug_interface(void);

#endif
