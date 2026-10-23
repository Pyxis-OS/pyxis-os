#ifndef KERNEL_ENDPOINT_H
#define KERNEL_ENDPOINT_H

#include <abi/endpoint.h>
#include <abi/describe.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/object/capability.h>
#include <kernel/service/request.h>

struct process;
struct endpoint_state;
struct native_completion;
struct readiness_source;
struct forwarder_operation;
struct file_object;
struct endpoint {
  struct kernel_object object;
  struct endpoint_state *state;
  struct process *owner;
  struct endpoint *owner_next;
};

struct endpoint_create_request {
  struct bsp_request request;
  struct process *owner; /* Kept alive by the admitted caller. */
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  struct endpoint_create_reply reply;
  enum call_status result;
};

struct endpoint_export_request {
  struct bsp_request request;
  struct process *owner; /* Kept alive by the admitted caller. */
  struct capability_reference receiver;
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  struct endpoint_export_message input;
  struct endpoint_export_reply reply;
  enum call_status result;
};

struct endpoint_describe_request {
  struct bsp_request request;
  struct process *owner; /* Kept alive by the admitted caller. */
  struct kernel_object *receiver; /* Owned storage, no borrowed user pointers. */
  uint64_t object_id;
  struct resource_description description;
  void *body_bytes;
  size_t body_size;
  struct file_object *read_file; /* Borrowed; parked caller lends operation ownership. */
  enum call_status result;
};

/* BSP, IF=0. Preserve atomic installation and rollback; caller owns completion. */
void endpoint_create_execute(struct endpoint_create_request *request);
void endpoint_export_execute(struct endpoint_export_request *request);
void endpoint_describe_execute(struct endpoint_describe_request *request);
/* IF=0, held export storage. Provider-free copy; optional body receives one
 * owned native FILE storage reference. Grant installation/release is outside
 * the endpoint guard. A closed export returns no body or cached metadata. */
enum call_status endpoint_description_capture(struct kernel_object *object,
    struct resource_description *description, struct file_object **body);
/* Logical client lifetime is independent of admitted storage references. */
bool endpoint_export_grant_retain(struct kernel_object *object);
void endpoint_export_grant_release(struct kernel_object *object);
/* Private forwarder storage does not keep provider state logically in use.
 * These own a physical reference as well as the caretaker count. */
bool endpoint_export_caretaker_retain(struct kernel_object *object);
void endpoint_export_caretaker_release(struct kernel_object *object);
/* Decrements ordinary client storage and elects final physical retirement. */
bool endpoint_export_storage_release(struct kernel_object *object);
/* CUT pins the receipt before invoking this outside the forwarder guard. */
void endpoint_forwarder_cancel(struct forwarder_operation *operation);
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

/* BSP, IF=0. Reserve before backend admission; commit accepts the correlation
 * ID and moves operation storage ownership to the delivery. Discard only an
 * uncommitted reservation; it leaves operation storage with its caller. */
enum call_status endpoint_completion_reserve(struct endpoint *receiver,
    struct native_completion *operation, struct capability_reservation *reservation,
    struct capability_reserved_slot *slots);
/* BSP, borrowed submitter context installed. Elects admission against CUT;
 * the completion's cloned backend context remains pending until effects. */
enum call_status endpoint_completion_commit(struct native_completion *operation);
void endpoint_completion_discard(struct native_completion *operation);
/* BSP, IF=0, after worker loans end. May acknowledge and release operation;
 * caller must not access it after publication. */
void endpoint_completion_publish(struct native_completion *operation);
/* Any CPU, IF=0, caller already holds the owned receiver authority. */
enum call_status endpoint_completion_cancel(struct endpoint *receiver, uint64_t id);

/* BSP, IF=0. Register each retained receiver interest before its first scan;
 * unregister before releasing that interest's object reference. */
void endpoint_readiness_register(struct endpoint *receiver);
void endpoint_readiness_unregister(struct endpoint *receiver);
/* Persistent subscriptions retain receiver storage and join source detach before
 * releasing it. The hook supplies no logical RECEIVE grant. */
struct readiness_source *endpoint_readiness_source(struct kernel_object *object);
/* Observe under the endpoint lock without consuming or reserving delivery. */
uint64_t endpoint_receiver_ready(struct endpoint *receiver, uint64_t events);

struct syscall_result endpoint_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result endpoint_call(struct kernel_object *object, handle_t handle,
    uint64_t rights, uint64_t transport, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);

#endif
