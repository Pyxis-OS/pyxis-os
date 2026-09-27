#ifndef KERNEL_ENDPOINT_H
#define KERNEL_ENDPOINT_H

#include <abi/endpoint.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct process;
struct endpoint_state;
struct endpoint {
  struct kernel_object object;
  struct endpoint_state *state;
  struct process *owner;
  struct endpoint *owner_next;
};

/* BSP, IF=0, with exclusive ownership of the caller's capability table.
 * Reserves all delivery storage and installs both handles or neither. */
enum call_status endpoint_create(struct process *owner, struct endpoint_create_reply *reply);
struct kernel_object *endpoint_service_create(void);
/* BSP, IF=0, before destroying an inactive process's capabilities. Closes all
 * owned receivers even when messages or client grants retain their objects. */
void endpoint_process_exit(struct process *owner);
/* Explicit CLOSE takes effect before returning; backing reclamation remains BSP-owned. */
void endpoint_handle_close(struct kernel_object *object);

struct syscall_result endpoint_service_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result endpoint_call(struct kernel_object *object, handle_t handle,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);

#endif
