#ifndef KERNEL_USER_READINESS_SET_H
#define KERNEL_USER_READINESS_SET_H

#include <abi/readiness_set.h>
#include <kernel/object/capability.h>
#include <kernel/object/forwarder.h>
#include <kernel/service/request.h>
#include <kernel/syscall.h>
#include <kernel/user/readiness_source.h>

struct process;
struct readiness_account;
struct readiness_set;
struct readiness_registration;
struct user_buffer_lease;

#define READINESS_CREATE UINT64_C(0)
#define READINESS_FINISH UINT64_MAX

struct readiness_set_request {
  struct bsp_request request;
  struct process *caller;
  struct readiness_set *set;
  struct kernel_object *created;
  struct capability_reference target;
  uint64_t operation, finish_operation;
  struct readiness_set_registration key;
  uint64_t allowed_events, active_events, observed_generation, tag;
  uint64_t deadline, capacity;
  uintptr_t events_address;
  struct readiness_set_registration registration_reply;
  struct readiness_set_wait_reply wait_reply;
  struct readiness_set_info info_reply;
  struct readiness_set_event events[READINESS_SET_BATCH_MAX];
  enum call_status status;
  struct readiness_registration *prepared;
  bool copied;
};

struct syscall_result readiness_set_create_call(uintptr_t reply, uint64_t capacity);
struct syscall_result readiness_set_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request, size_t size,
    uintptr_t reply, size_t capacity);

enum call_status readiness_set_publish(struct readiness_set_request *request,
    const struct user_buffer_lease *lease, uintptr_t reply, size_t capacity);
void readiness_set_abandon(struct kernel_object *object);

void readiness_set_submit(struct readiness_set_request *request);
bool readiness_sets_service(bool network);
bool readiness_sets_deadline(bool network, uint64_t *deadline);
bool readiness_sets_pending(bool network);
void readiness_set_process_stop(struct process *process);
void readiness_set_process_exit(struct process *process);
bool readiness_set_grant_retain(struct kernel_object *object, uint64_t rights);
void readiness_set_grant_release(struct kernel_object *object, uint64_t rights);
bool readiness_set_owned(const struct kernel_object *object, struct process *caller);
uint64_t readiness_set_ready(struct kernel_object *object, struct process *caller);

/* IF=0, forwarder guard held. Only queues work under the readiness guard;
 * never resolves/releases lineage, queries a source or allocates storage. */
void readiness_registration_revoked(struct forwarder_operation *operation);

#endif
