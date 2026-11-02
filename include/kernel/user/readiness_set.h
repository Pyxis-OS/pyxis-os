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
struct readiness_process_watch;
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
  struct call_limit limit;
  struct readiness_registration *prepared;
  struct readiness_process_watch *process_watch;
  const struct capability_grant *observer;
  bool copied;
};

struct syscall_result readiness_set_create_call(uintptr_t reply, uint64_t capacity,
    struct call_limit *detail);
struct syscall_result readiness_set_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request, size_t size,
    uintptr_t reply, size_t capacity, struct call_limit *detail);

enum call_status readiness_set_publish(struct readiness_set_request *request,
    const struct user_buffer_lease *lease, uintptr_t reply, size_t capacity);
void readiness_set_abandon(struct kernel_object *object);

/* Internal observed launch. PREPARE/BIND/FINISH enter and return on the current
 * user caller with IF=0 and no guards. They may park through the catalog's
 * readiness-set request while the set's BSP worker owns metadata.
 * PREPARE always consumes/clears set_reference, retaining storage rather than
 * a CONTROL grant. It reserves a hidden, loaned registration before launch.
 * BIND borrows the unpublished child's real observer grant until return,
 * retaining only control storage and independent pending source lineage.
 * PREPARE failure leaves no token. After admission, failure leaves the token
 * owned by its preparer for FINISH(false). */
enum call_status readiness_process_watch_prepare(struct process *owner,
    struct capability_reference *set_reference, uint64_t tag,
    struct readiness_process_watch **watch, struct call_limit *detail);
enum call_status readiness_process_watch_bind(struct readiness_process_watch *watch,
    const struct capability_grant *observer, struct call_limit *detail);

/* Current admitted user caller, IF=0, no guards. Elects launch start against
 * CUT of the SET argument and launcher; never starts persistent source lineage. */
bool readiness_process_watch_start(struct readiness_process_watch *watch,
    struct forwarder_operation *launch_lineage);

/* Any CPU, IF=0, final group admission serialized. All other fallible launch
 * checks must already have passed. Success commits the watch before the
 * non-failing child queue/observer installation; only that sequence may follow.
 * The key is kernel-owned storage. Closure/CUT after this point uses ordinary
 * observation cancellation; failure leaves the child unpublished. */
enum call_status readiness_process_watch_commit(struct readiness_process_watch *watch,
    struct readiness_set_registration *registration);

/* Consumes the token exactly once, after all preparation and reply work.
 * True follows a committed launch and confirmed user reply: release the loan
 * and freshly check completion. False joins and discards an unclaimed watch,
 * including a committed watch after late reply failure; it never stops a child.
 * The loan remains held through child queueing and user reply publication. */
void readiness_process_watch_finish(struct readiness_process_watch *watch, bool published);

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
