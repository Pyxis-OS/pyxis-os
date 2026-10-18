#ifndef KERNEL_OBJECT_NET_FACTORY_H
#define KERNEL_OBJECT_NET_FACTORY_H

#include <kernel/net/factory.h>
#include <kernel/object/capability.h>
#include <kernel/service/request.h>
#include <kernel/syscall.h>

struct net_factory_derive_service_request {
  struct bsp_request request;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  const struct net_interface *interface; /* Retained until reboot. */
  enum object_type type;
  uint64_t rights;
  handle_t result;
  enum call_status status;
};

struct syscall_result net_factory_next_interface_call(struct net_factory *factory,
    uint64_t rights, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result net_factory_derive_call(struct net_factory *factory,
    uint64_t rights, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
/* BSP executor, IF=0. Captured identity/rights only; no user pointers. */
void net_factory_derive_execute(struct net_factory_derive_service_request *request);

#endif
