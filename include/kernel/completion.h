#ifndef KERNEL_COMPLETION_H
#define KERNEL_COMPLETION_H

#include <abi/completion.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <kernel/object/forwarder.h>
#include <kernel/service/request.h>

struct endpoint;
struct endpoint_delivery_record;
struct process;
struct completion_gate;
struct process_control;

struct native_completion {
  struct kernel_object storage;
  struct capability_reference target, source;
  struct process_control *actor;
  struct forwarder_operation lineage;
  struct endpoint_delivery_record *delivery;
  struct completion_gate *gate;
  struct execution_group *cleanup_group;
  uint64_t protocol, operation, deadline_ns, correlation_id;
  size_t input_size, output_size, charged_bytes, network_bytes;
  uint64_t progress;
  bool progress_known;
  uint8_t input[COMPLETION_PAYLOAD_MAX], output[COMPLETION_PAYLOAD_MAX];
  struct capability_grant returned;
  enum call_status result;
  atomic_uint_fast64_t control; /* Started and cancellation election. */
  void *backend; /* Sole owning subsystem, cleared before finish. */
};

struct completion_admit_request {
  struct bsp_request request;
  struct process *owner; /* Only the synchronous admission handoff. */
  struct capability_reference target, receiver, source;
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  uint64_t protocol, operation, deadline_ns, correlation_id;
  size_t input_size;
  uint8_t input[COMPLETION_PAYLOAD_MAX];
  enum call_status result;
};

/* Owned operation. Reserve additional completion metadata before allocation/effects.
 * Charge belongs to the operation through ACK/drain and deferred reclamation. */
enum call_status native_completion_charge(struct native_completion *operation, size_t bytes);

/* BSP/IF=0. Reserve native admission without starting I/O. Discard only before
 * acceptance; start transfers backend ownership through finish. */
enum call_status native_completion_net_reserve(struct native_completion *operation);
void native_completion_net_start(struct native_completion *operation);
void native_completion_net_discard(struct native_completion *operation);
/* Any CPU/IF=0, may hold endpoint lock: notify only, no nested subsystem lock. */
void native_completion_net_cancel(struct native_completion *operation);
enum call_status native_completion_fs_reserve(struct native_completion *operation);
void native_completion_fs_start(struct native_completion *operation);
void native_completion_fs_discard(struct native_completion *operation);
void native_completion_fs_cancel(struct native_completion *operation);

/* Worker owns operation until finish. Mark start atomically refuses prior cancel
 * or expiry; true does not promise no later cancellation. Never replay effects. */
bool native_completion_mark_started(struct native_completion *operation);
enum call_status native_completion_cancel_reason(struct native_completion *operation);
/* BSP/IF=0, after clearing worker loans/backend. Moves grant; output may be NULL
 * for zero bytes. No operation access after this ownership transfer. */
void native_completion_finish(struct native_completion *operation, enum call_status status,
    const void *output, size_t size, struct capability_grant *grant);
/* Endpoint lock, any CPU/IF=0. Cancellation and terminal publication serialize. */
void native_completion_request_cancel(struct native_completion *operation, enum call_status reason);
/* Endpoint lock, no worker ownership. Releases delivery ownership; independent
 * storage and its process metadata charge are reclaimed on BSP. */
void native_completion_ack(struct native_completion *operation);
uint64_t native_completion_flags(struct native_completion *operation);
void completion_admit_execute(struct completion_admit_request *request);
void native_completion_process_exit(struct process *process);
struct syscall_result native_completion_call(struct kernel_object *target, uint64_t rights,
    uint64_t transport, uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
