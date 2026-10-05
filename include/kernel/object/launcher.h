#ifndef KERNEL_LAUNCHER_H
#define KERNEL_LAUNCHER_H

#include <abi/launcher.h>
#include <abi/syscall.h>
#include <kernel/service/request.h>

struct kernel_object;
struct process;
struct launch_capture;
struct launch_preparation;
struct execution_group;

enum launcher_action {
  LAUNCH_CREATE_EXECUTION_GROUP,
  LAUNCH_ALLOCATE,
  LAUNCH_DISCARD,
  LAUNCH_START,
  LAUNCH_BATCH_CREATE,
  LAUNCH_BATCH_PREPARE,
  LAUNCH_BATCH_PUBLISH,
  LAUNCH_BATCH_DISCARD,
};

/* Capture/batch storage survives successive requests independently. START and
 * BATCH_PREPARE lend the parent table and stable image operation; every loan
 * is cleared before completion. CREATE_EXECUTION_GROUP also lends the parent
 * table for atomic handle installation. Allocation results transfer to the caller
 * before release, while START/DISCARD/PREPARE consume their capture and
 * PUBLISH/DISCARD consume their batch. No caller private mappings are mutated. */
struct launcher_request {
  struct bsp_request request;
  enum launcher_action action;
  struct launch_capture *capture;
  struct launch_preparation *group;
  struct process *parent;
  struct execution_group *execution_group; /* Borrowed from caller's launcher. */
  /* Placement tie-break only: the caller stays in this syscall, on this CPU,
   * until the request completes. Never used for admission. */
  size_t parent_cpu;
  struct launch_capture *capture_result;
  struct launch_preparation *group_result;
  enum call_status result;
  handle_t child;
  handle_t children[LAUNCH_BATCH_MAX];
  struct execution_group_create_reply execution_reply;
};

/* BSP executor, IF=0. Local launch helpers never submit nested requests. */
void launcher_request_execute(struct launcher_request *request);

/* BSP, IF=0. Stateless, caller-scoped launch authority; one owned reference. */
struct kernel_object *launcher_create(void);
/* Immutable binding; NULL for the ordinary caller-scoped launcher. */
struct execution_group *launcher_execution_group(const struct kernel_object *object);

/* Current user task, IF=0, checked protocol tag. Captures input on caller CPU;
 * BSP owns preparation and allocation. May sleep, with no held spinlocks. */
struct syscall_result launcher_call(struct kernel_object *object, uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
