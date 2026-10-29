#ifndef KERNEL_EXECUTION_GROUP_H
#define KERNEL_EXECUTION_GROUP_H

#include <abi/execution_group.h>
#include <abi/handle.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct space;
struct task;
struct process;
struct task_wait_link;
struct capability_reservation;
struct capability_reserved_slot;
struct capability_grant;

struct execution_group_member {
  struct execution_group_member *next;
  struct process *process;
};

/* Immutable space/parent/depth. The space tree guard protects admission, active
 * child links, controlling grants, member links and completion counters. A process owns one storage reference from preparation
 * through final reclamation; only published members contribute to members. */
struct execution_group {
  struct kernel_object object;
  struct space *space;
  struct execution_group *parent; /* Owned until final storage destruction. */
  size_t depth;
  struct execution_group *first_child;
  struct execution_group *next_sibling;
  size_t children; /* Active child edges own storage, not controlling authority. */
  bool sealed;
  bool stopping;
  bool complete;
  atomic_uint_fast64_t result_flags;
  size_t controllers;
  size_t members;
  size_t launches;
  size_t cleanup_pending;
  struct execution_group_member *first_member;
  struct task_wait_link *waiters;
};

/* BSP, IF=0. Returns one storage reference, no controlling authority. */
enum call_status execution_group_create(struct space *space,
    struct execution_group *parent, struct execution_group **result);
/* BSP, IF=0. Publish the owned pair under final tree admission exclusion.
 * All grants/slots exist and are validated; no allocation or user access. */
enum call_status execution_group_install(struct execution_group *group,
    struct capability_reservation *reservation,
    const struct capability_reserved_slot *slots, struct capability_grant *grants,
    handle_t *handles);
/* Failure before controlling authority exists must still release an active edge. */
void execution_group_abandon(struct execution_group *group);
/* Immutable ancestry; callers retain descendant storage. NULL scope permits
 * ordinary ungrouped authority; a member rejects NULL/foreign/ancestor targets. */
bool execution_group_contains(const struct execution_group *scope,
    const struct execution_group *target);

/* IF=0, caller retains storage. No allocation. Another space is DENIED;
 * sealed admission is ENDPOINT_CLOSED. Publication rechecks under the lock. */
enum call_status execution_group_check(struct execution_group *group, struct space *space);

/* Internal sibling publication, BSP/IF=0, retained process group. Success owns
 * the group guard until end; stopping refuses, SEAL still permits siblings.
 * Lock order: space tree -> process lifetime -> table/ready queues. NULL is allowed. */
bool execution_group_thread_begin(struct execution_group *group);
void execution_group_thread_end(struct execution_group *group);

/* IF=0, exclusive preparation actor. Prepared processes own group references.
 * Serialize final admission/enrollment with sealing, then transfer every task.
 * Failure leaves all tasks unpublished and owned by the preparer. */
enum call_status execution_group_publish(struct execution_group *group,
    struct task **tasks, size_t count);
/* IF=0, retained retirement owner, after process, kernel stack and task storage
 * have been reclaimed. Removes one published member and consumes its storage reference. */
void execution_group_member_complete(struct execution_group *group);

/* Admitted preparation owns storage until all staging has been disposed. */
enum call_status execution_group_launch_begin(struct execution_group *group,
    struct space *space);
void execution_group_launch_end(struct execution_group *group);
void execution_group_member_detach(struct execution_group *group,
    struct execution_group_member *member);
/* Each pending cleanup owns one storage reference; callbacks may transfer it. */
void execution_group_cleanup_begin(struct execution_group *group);
void execution_group_cleanup_end(struct execution_group *group);
uint64_t execution_group_ready(struct execution_group *group);
/* Storage retained, immutable ancestry. Atomic-only, including under a table
 * guard: no callbacks/locks. Active admission keeps completion unpublished. */
void execution_group_note(struct execution_group *group, uint64_t flags);

bool execution_group_authority_retain(struct kernel_object *object, uint64_t rights);
void execution_group_authority_release(struct kernel_object *object, uint64_t rights);
struct syscall_result execution_group_call(struct execution_group *group,
    uint64_t rights, uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
