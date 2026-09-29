#ifndef KERNEL_ENDPOINT_H
#define KERNEL_ENDPOINT_H

#include <abi/endpoint.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct process;
struct endpoint_state;
struct endpoint {
  struct kernel_object object;
  struct endpoint_state *state;
  struct process *owner;
  struct endpoint *owner_next;
};

struct endpoint_create_request {
  struct bsp_request request;
  struct process *loan; /* Exclusive capability table and endpoint owner list. */
  struct endpoint_create_reply reply;
  enum call_status result;
};

struct endpoint_export_request {
  struct bsp_request request;
  struct process *loan; /* Exclusive table; its receiver authority stays live. */
  struct endpoint_export_message input;
  struct endpoint_export_reply reply;
  enum call_status result;
};

/* BSP, IF=0. Preserve atomic installation and rollback; caller owns completion. */
void endpoint_create_execute(struct endpoint_create_request *request);
void endpoint_export_execute(struct endpoint_export_request *request);
bool endpoint_export_authority_valid(const struct kernel_object *object,
    uint64_t rights, uint64_t transport);
/* IF=0, held export reference; closure can still race after this snapshot. */
bool endpoint_export_available(struct kernel_object *object);
uint64_t endpoint_export_protocol(const struct kernel_object *object);
struct kernel_object *endpoint_service_create(void);
/* BSP, IF=0, before destroying an inactive process's capabilities. Closes all
 * owned receivers even when messages or client grants retain their objects. */
void endpoint_process_exit(struct process *owner);
/* Explicit CLOSE takes effect before returning; backing reclamation remains BSP-owned. */
void endpoint_handle_close(struct kernel_object *object);
/* IF=0, final receipt reference already dropped, endpoint lock not held.
 * Releases logical delivery ownership; backing destruction remains on BSP. */
void endpoint_receipt_release(struct kernel_object *object);

struct syscall_result endpoint_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result endpoint_call(struct kernel_object *object, handle_t handle,
    uint64_t rights, uint64_t transport, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);

#endif
