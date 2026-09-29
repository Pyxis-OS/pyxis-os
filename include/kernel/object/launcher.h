#ifndef KERNEL_LAUNCHER_H
#define KERNEL_LAUNCHER_H

#include <abi/launcher.h>
#include <abi/syscall.h>
#include <kernel/service/request.h>

struct kernel_object;
struct process;
struct launch_capture;
struct launch_group;

enum launcher_action {
  LAUNCH_ALLOCATE,
  LAUNCH_DISCARD,
  LAUNCH_START,
  LAUNCH_GROUP_CREATE,
  LAUNCH_GROUP_PREPARE,
  LAUNCH_GROUP_PUBLISH,
  LAUNCH_GROUP_DISCARD,
};

/* Capture/group storage survives successive requests independently. START and
 * GROUP_PREPARE lend the parent table and stable image operation; every loan
 * is cleared before completion. Allocation results transfer to the caller
 * before release, while START/DISCARD/PREPARE consume their capture and
 * PUBLISH/DISCARD consume their group. No caller private mappings are mutated. */
struct launcher_request {
  struct bsp_request request;
  enum launcher_action action;
  struct launch_capture *capture;
  struct launch_group *group;
  struct process *parent;
  size_t cpu_index;
  struct launch_capture *capture_result;
  struct launch_group *group_result;
  enum call_status result;
  handle_t child;
  handle_t children[LAUNCH_BATCH_MAX];
};

/* BSP executor, IF=0. Local launch helpers never submit nested requests. */
void launcher_request_execute(struct launcher_request *request);

/* BSP, IF=0. Stateless, caller-scoped launch authority; one owned reference. */
struct kernel_object *launcher_create(void);

/* Current user task, IF=0, checked protocol tag. Captures input on caller CPU;
 * BSP owns preparation and allocation. May sleep, with no held spinlocks. */
struct syscall_result launcher_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
