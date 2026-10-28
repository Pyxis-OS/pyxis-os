#ifndef KERNEL_OBJECT_FORWARDER_H
#define KERNEL_OBJECT_FORWARDER_H

#include <abi/forwarder.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

/* Tunable implementation budget for one operation/derived grant, not an ABI. */
#define FORWARDER_LINEAGE_MAX 32

struct forwarder;
struct task_wait;

enum forwarder_operation_state {
  FORWARDER_PENDING,
  FORWARDER_STARTED,
  FORWARDER_DENIED,
};

/* Stable shared storage. A pending operation owns every selected lineage node;
 * retarget does not change its captured target. A worker may borrow this record
 * only while the caller is parked, or own an explicit cloned record. */
struct forwarder_operation {
  struct forwarder_operation *next;
  struct forwarder_operation **previous;
  struct forwarder *nodes[FORWARDER_LINEAGE_MAX];
  size_t count;
  enum forwarder_operation_state state;
  struct task_wait *wait;
  struct kernel_object *notification_owner; /* Borrowed live queued receipt. */
  struct forwarder_operation *notification_next;
  bool readiness_observer;
};

struct forwarder_request {
  struct bsp_request request;
  struct capability_grant target;
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  const struct forwarder_operation *lineage;
  uint64_t kind;
  struct forwarder_reply reply;
  enum call_status result;
  bool derive;
};

/* IF=0, no table guard. Unmodified grant capture keeps the wrapper; only
 * operation resolution extracts target storage and adds its lineage. */
enum call_status forwarder_resolve(struct kernel_object *object,
    uint64_t rights, uint64_t transport, struct forwarder_operation *operation,
    struct capability_reference *target);
/* Table -> forwarder guard; storage retain only, no callbacks or operations.
 * Retains the current leaf so CLOSE cannot naturally retire an admitted export
 * between original-wrapper acquisition and later operation resolution. Optional
 * restrictions receive the union of the walked target grants', from the same
 * walk as the leaf. Resolution likewise adds them to the target reference. */
enum capability_result forwarder_snapshot(struct kernel_object *object,
    struct kernel_object **target, uint64_t *restrictions);
void forwarder_operation_init(struct forwarder_operation *operation);
enum call_status forwarder_operation_clone(struct forwarder_operation *destination,
    const struct forwarder_operation *source);
void forwarder_operation_release(struct forwarder_operation *operation);
/* Atomically elects start against CUT. Started operations keep their real result. */
bool forwarder_operation_start(struct forwarder_operation *operation);
bool forwarder_operation_start_pair(struct forwarder_operation *first,
    struct forwarder_operation *second);
bool forwarder_operation_denied(const struct forwarder_operation *operation);
void forwarder_operation_wait(struct forwarder_operation *operation, struct task_wait *wait);
void forwarder_operation_unwait(struct forwarder_operation *operation, struct task_wait *wait);
/* Queue registration; CUT pins this receipt before notifying outside its guard. */
void forwarder_operation_delivery_owner(struct forwarder_operation *operation,
    struct kernel_object *receipt);
/* IF=0; the owning registration keeps operation alive until disabled/released.
 * CUT callbacks run synchronously under the forwarder guard and only queue
 * work under the readiness guard. CUT wakes workers after dropping its guard.
 * Disabling joins the callback. Enabling an already denied operation queues
 * work before returning; its caller must be servicing that queue or wake the
 * worker after this call. */
void forwarder_operation_readiness(struct forwarder_operation *operation, bool enabled);

enum object_type forwarder_type(const struct kernel_object *object);
uint64_t forwarder_protocol(const struct kernel_object *object);
bool forwarder_authority_valid(const struct kernel_object *object,
    uint64_t rights, uint64_t transport);
bool forwarder_available(const struct kernel_object *object);
enum call_status forwarder_status(const struct kernel_object *object);
bool forwarder_grant_retain(struct kernel_object *object, uint64_t rights);
void forwarder_grant_release(struct kernel_object *object, uint64_t rights);
/* Called instead of ordinary storage decrement. True elects final retirement. */
bool forwarder_storage_release(struct kernel_object *object);

/* Newly allocated wrapper metadata for a derived grant; retained target storage
 * and embedded lineage are accounted by their existing owners. */
size_t forwarder_derived_storage_size(const struct forwarder_operation *lineage);
/* Retained kernel-root actor, IF=0. Moves a grant into a derived forwarder before external effects;
 * ordinary operations without lineage take the existing no-allocation path. */
enum capability_result forwarder_derive_grant(struct capability_grant *grant,
    const struct forwarder_operation *lineage);
/* Current user, no locks; BSP allocation when lineage is present. */
enum capability_result forwarder_request_derivation(struct capability_grant *grant);
/* Explicit delivered-operation lineage remains owned across the BSP wait. */
enum capability_result forwarder_request_derivation_from(struct capability_grant *grant,
    const struct forwarder_operation *lineage);
/* BSP creation for endpoint exports; consumes target on success only. */
enum call_status forwarder_create_owned(struct capability_grant *target,
    uint64_t kind, struct capability_grant *holder, struct capability_grant *controller);
void forwarder_execute(struct forwarder_request *request);
struct syscall_result forwarder_create_call(uintptr_t request, uintptr_t reply,
    uint64_t capacity);
struct syscall_result forwarder_controller_call(struct kernel_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request, size_t size);

#endif
