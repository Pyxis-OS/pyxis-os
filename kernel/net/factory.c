#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/net/registry.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/factory.h>
#include <kernel/panic.h>

static void destroy_factory(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *net_factory_create(enum object_type type,
    const struct net_interface *const *interfaces, size_t count)
{
  KASSERT(type == OBJECT_TCP_SERVICE || type == OBJECT_UDP_SERVICE || type == OBJECT_ECHO);
  if (!count || count > (SIZE_MAX - sizeof(struct net_factory)) / sizeof(*interfaces)) {
    return NULL;
  }
  for (size_t i = 0; i < count; ++i) {
    KASSERT(interfaces[i]);
  }
  struct net_factory *factory = kmalloc(sizeof(*factory) + count * sizeof(*interfaces));
  if (!factory) {
    return NULL;
  }
  object_init(&factory->object, type, destroy_factory);
  factory->interface_count = count;
  memcpy(factory->interfaces, interfaces, count * sizeof(*interfaces));
  return &factory->object;
}

bool net_factory_allows(const struct net_factory *factory,
    const struct net_interface *interface)
{
  for (size_t i = 0; i < factory->interface_count; ++i) {
    if (factory->interfaces[i] == interface) {
      return true;
    }
  }
  return false;
}

enum call_status net_factory_path(const struct net_factory *factory,
    struct net_path path, const struct net_interface **pinned)
{
  if (path.reserved) {
    return CALL_BAD_REQUEST;
  }
  *pinned = NULL;
  if (!path.interface_id) {
    return CALL_OK;
  }
  for (size_t i = 0; i < factory->interface_count; ++i) {
    if (factory->interfaces[i]->id == path.interface_id) {
      *pinned = factory->interfaces[i];
      return CALL_OK;
    }
  }
  return CALL_DENIED;
}

enum call_status net_factory_bind(const struct net_factory *factory,
    const struct net_interface *pinned, uint32_t source, struct ipv4_route *route)
{
  net_worker_assert_context();
  if (pinned && !net_factory_allows(factory, pinned)) {
    return CALL_DENIED;
  }
  const struct net_interface *owner = net_interface_for_address(source);
  if (!owner) {
    return pinned ? CALL_BAD_REQUEST : CALL_UNAVAILABLE;
  }
  if (pinned && pinned != owner) {
    return CALL_BAD_REQUEST;
  }
  if (!net_factory_allows(factory, owner)) {
    return CALL_DENIED;
  }
  enum net_result result = net_ipv4_route_on(owner, source, source, route);
  return result == NET_OK ? CALL_OK : CALL_UNAVAILABLE;
}
