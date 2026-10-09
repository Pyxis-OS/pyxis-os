#ifndef KERNEL_EXECUTION_GROUP_H
#define KERNEL_EXECUTION_GROUP_H

#include <abi/execution_group.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct space;
struct task;
struct process;
struct task_wait_link;

struct execution_group_member {
  struct execution_group_member *next;
  struct process *process;
};

/* Immutable space. Lock protects admission, controlling grants, member links
 * and completion counters. A process owns one storage reference from preparation
 * through final reclamation; only published members contribute to members. */
struct execution_group {
  struct kernel_object object;
  struct space *space;
  atomic_bool locked;
  bool sealed;
  bool stopping;
  bool complete;
  size_t controllers;
  size_t members;
  size_t launches;
  size_t cleanup_pending;
  struct execution_group_member *first_member;
  struct task_wait_link *waiters;
};

/* BSP, IF=0. Returns one storage reference, no controlling authority. */
struct execution_group *execution_group_create(struct space *space);

/* IF=0, caller retains storage. No allocation. Another space is DENIED;
 * sealed admission is ENDPOINT_CLOSED. Publication rechecks under the lock. */
enum call_status execution_group_check(struct execution_group *group, struct space *space);

/* BSP, IF=0. All prepared processes already own group storage references. Serialize final
 * admission and enrollment with sealing, then transfer every task together.
 * Failure leaves all tasks unpublished and owned by the preparer. */
enum call_status execution_group_publish(struct execution_group *group,
    struct task **tasks, size_t count);
/* BSP, IF=0, after process, kernel stack and task storage have been reclaimed.
 * Removes one published member and consumes its storage reference. */
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

bool execution_group_authority_retain(struct kernel_object *object, uint64_t rights);
void execution_group_authority_release(struct kernel_object *object, uint64_t rights);
struct syscall_result execution_group_call(struct execution_group *group,
    uint64_t rights, uint64_t operation, size_t request_size);

#endif
