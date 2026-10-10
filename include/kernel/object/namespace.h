#ifndef KERNEL_NAMESPACE_H
#define KERNEL_NAMESPACE_H

#include <abi/namespace.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/object/capability.h>
#include <kernel/service/request.h>
#include <stdbool.h>

struct capability_table;

struct namespace_create_request {
  struct bsp_request request;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  handle_t handle;
  enum call_status result;
};

/* BSP executor, IF=0, with a reserved destination slot. */
void namespace_create_execute(struct namespace_create_request *request);

/* BSP, IF=0. Creation publishes into the caller's reserved slot. */
struct kernel_object *namespace_service_create(void);

/* IF=0, caller holds a namespace reference. Presence includes dead exports;
 * this checks name ambiguity, not liveness or lookup authority. */
bool namespace_has_name(struct kernel_object *object, const char *name);

struct syscall_result namespace_service_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result namespace_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
