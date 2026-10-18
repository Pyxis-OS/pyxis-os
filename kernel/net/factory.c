#include <kernel/memory.h>
#include <kernel/mm/heap.h>
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
