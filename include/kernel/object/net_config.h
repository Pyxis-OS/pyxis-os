#ifndef KERNEL_OBJECT_NET_CONFIG_H
#define KERNEL_OBJECT_NET_CONFIG_H

#include <kernel/net/config.h>
#include <kernel/object/capability.h>
#include <kernel/service/request.h>
#include <kernel/syscall.h>

struct net_control_object {
  struct kernel_object object;
  struct net_interface *interface; /* Retained until reboot; NULL for admin/registry. */
};

enum net_control_stage { NET_CONTROL_ALLOCATE, NET_CONTROL_INSTALL, NET_CONTROL_RELEASE };
struct net_control_service_request {
  struct bsp_request request;
  enum net_control_stage stage;
  struct net_control control;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  enum object_type type;
  uint64_t rights;
  handle_t result;
  enum call_status status;
};

/* BSP/IF=0. Independent authority; one owned reference. */
struct kernel_object *net_registry_create(void);
struct kernel_object *net_policy_create(void);
struct syscall_result net_control_call(struct net_control_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
void net_control_execute(struct net_control_service_request *request);

#endif
