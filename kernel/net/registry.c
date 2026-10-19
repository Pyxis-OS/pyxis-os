#include <kernel/net/config.h>
#include <kernel/net/lwip.h>
#include <kernel/net/driver.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <stdatomic.h>
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

struct net_interface *net_interface_for_id(uint64_t id)
{
  net_worker_assert_context();
  for (struct net_interface *interface = net_interface_first(); interface;
      interface = interface->registry_next) {
    if (interface->id == id) {
      return interface;
    }
  }
  return NULL;
}

/* The dependency represents projection indices in eight bits, reserving zero. */
#define NET_PROJECTION_LIMIT 255
static size_t interface_count = 1;
static uint64_t next_id = 2;
static struct net_policy_reply policy;
static _Atomic(const struct net_interface *) default_interface;
static _Atomic(const struct net_interface *) remote_interface;
static _Atomic(const struct net_interface *) log_interface;
static _Atomic(const struct net_interface *) debug_interface;

static bool valid_name(const struct net_interface_definition *definition)
{
  const char *prefix = definition->interface_class == NET_INTERFACE_CLASS_ETHERNET ?
    "eth" : definition->interface_class == NET_INTERFACE_CLASS_WIFI ? "wlan" : NULL;
  if (!prefix || definition->reserved) {
    return false;
  }
  size_t at = strlen(prefix);
  if (memcmp(definition->name, prefix, at) ||
      definition->name[at] < '0' || definition->name[at] > '9') {
    return false;
  }
  if (definition->name[at] == '0' && definition->name[at + 1]) {
    return false;
  }
  while (at < sizeof(definition->name) && definition->name[at] >= '0' &&
      definition->name[at] <= '9') {
    ++at;
  }
  if (at == sizeof(definition->name)) {
    return false;
  }
  for (; at < sizeof(definition->name); ++at) {
    if (definition->name[at]) {
      return false;
    }
  }
  return true;
}

enum call_status net_registry_define(struct net_control *control)
{
  net_worker_assert_context();
  if (control->count > NET_PROJECTION_LIMIT - interface_count ||
      control->count > UINT64_MAX - next_id) {
    return CALL_LIMIT;
  }
  for (size_t i = 0; i < control->count; ++i) {
    const struct net_interface_definition *definition = &control->entries[i].definition;
    if (!valid_name(definition)) {
      return CALL_BAD_REQUEST;
    }
    for (const struct net_interface *other = net_interface_first(); other;
        other = other->registry_next) {
      if (strlen(other->name) == strlen(definition->name) &&
          !memcmp(other->name, definition->name, strlen(other->name))) {
        return CALL_ALREADY_EXISTS;
      }
    }
    for (size_t j = 0; j < i; ++j) {
      if (!memcmp(control->entries[j].definition.name, definition->name, sizeof(definition->name))) {
        return CALL_ALREADY_EXISTS;
      }
    }
  }
  struct net_interface *tail = net_interface_first();
  while (tail->registry_next) {
    tail = tail->registry_next;
  }
  for (size_t i = 0; i < control->count; ++i) {
    struct net_config_entry *entry = &control->entries[i];
    struct net_interface *interface = entry->interface;
    memcpy((char *)interface->name, entry->definition.name, sizeof(entry->definition.name));
    interface->id = next_id++;
    interface->interface_class = entry->definition.interface_class;
    interface->mtu = NET_PACKET_MAX_BYTES;
    interface->route_revision = 1;
    interface->ipv4.address_epoch = 1;
    net_lwip_publish_interface(interface);
    tail->registry_next = interface;
    tail = interface;
    entry->published = true;
    ++interface_count;
  }
  return CALL_OK;
}

static uint64_t interface_id(const struct net_interface *interface)
{
  return interface ? interface->id : 0;
}

enum call_status net_policy_replace(const struct net_policy_request *request)
{
  net_worker_assert_context();
  const uint64_t ids[] = {request->default_interface_id, request->dns_interface_id,
    request->remote_interface_id, request->log_interface_id, request->debug_interface_id};
  const struct net_interface *selected[5] = {0};
  for (size_t i = 0; i < 5; ++i) {
    if (ids[i] && !(selected[i] = net_interface_for_id(ids[i]))) {
      return CALL_NOT_FOUND;
    }
  }
  if (request->dns_mode == NET_DNS_NONE) {
    if (request->dns_interface_id || request->dns_server) {
      return CALL_BAD_REQUEST;
    }
  } else if (request->dns_mode == NET_DNS_NUMERIC) {
    if (!selected[1] || !net_control_valid_server(request->dns_server)) {
      return CALL_BAD_REQUEST;
    }
  } else if (request->dns_mode == NET_DNS_DHCP) {
    if (!selected[1] || request->dns_server) {
      return CALL_BAD_REQUEST;
    }
  } else {
    return CALL_BAD_REQUEST;
  }
  if (policy.revision == UINT64_MAX) {
    return CALL_LIMIT;
  }
  policy = (struct net_policy_reply){
    .default_interface_id = interface_id(selected[0]),
    .dns_interface_id = interface_id(selected[1]),
    .remote_interface_id = interface_id(selected[2]),
    .log_interface_id = interface_id(selected[3]),
    .debug_interface_id = interface_id(selected[4]),
    .revision = policy.revision + 1,
    .dns_server = request->dns_server, .dns_mode = request->dns_mode,
  };
  atomic_store_explicit(&default_interface, selected[0], memory_order_release);
  atomic_store_explicit(&remote_interface, selected[2], memory_order_release);
  atomic_store_explicit(&log_interface, selected[3], memory_order_release);
  atomic_store_explicit(&debug_interface, selected[4], memory_order_release);
  net_driver_panic_select(selected[3] && selected[3]->driver.kind != NET_DRIVER_KIND_NONE ?
      selected[3] : NULL);
  return CALL_OK;
}

void net_policy_snapshot(struct net_policy_reply *reply)
{
  net_worker_assert_context();
  *reply = policy;
  if (policy.dns_mode == NET_DNS_DHCP) {
    const struct net_interface *interface = net_interface_for_id(policy.dns_interface_id);
    reply->dns_server = interface ? interface->dns_server : 0;
  }
  if (reply->dns_server) {
    reply->flags |= NET_POLICY_DNS_VALID;
  }
}

const struct net_interface *net_policy_default_interface(void)
{
  return atomic_load_explicit(&default_interface, memory_order_acquire);
}

const struct net_interface *net_policy_remote_interface(void)
{
  return atomic_load_explicit(&remote_interface, memory_order_acquire);
}

const struct net_interface *net_policy_log_interface(void)
{
  return atomic_load_explicit(&log_interface, memory_order_acquire);
}

const struct net_interface *net_policy_debug_interface(void)
{
  return atomic_load_explicit(&debug_interface, memory_order_acquire);
}
