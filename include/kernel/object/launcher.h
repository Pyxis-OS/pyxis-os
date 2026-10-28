#ifndef KERNEL_LAUNCHER_H
#define KERNEL_LAUNCHER_H

#include <abi/launcher.h>
#include <abi/syscall.h>
#include <kernel/service/request.h>
#include <kernel/object/capability.h>

struct kernel_object;
struct process;
struct launch_capture;
struct execution_group;

enum launcher_action {
  LAUNCH_CREATE_EXECUTION_GROUP,
  LAUNCH_CAPTURE_RAM,
  LAUNCH_CREATE_SPACE,
  LAUNCH_SPACE_DEVICES,
  LAUNCH_SPACE_FINISH,
};

/* Fixed-owner setup/capture requests borrow stable heap storage from the
 * retained caller. Heavy child preparation runs on the caller's kernel root;
 * SPACE_DEVICES borrows its exclusive unpublished child until notification. */
struct launcher_request {
  struct bsp_request request;
  enum launcher_action action;
  struct launch_capture *capture;
  struct process *parent;
  struct execution_group *execution_group; /* Borrowed from caller's launcher. */
  enum call_status result;
  struct execution_group_create_reply execution_reply;
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
};

/* BSP executor, IF=0. Local launch helpers never submit nested requests. */
void launcher_request_execute(struct launcher_request *request);

/* BSP, IF=0. Stateless, caller-scoped launch authority; one owned reference. */
struct kernel_object *launcher_create(void);
/* Immutable binding; NULL for the ordinary caller-scoped launcher. */
struct execution_group *launcher_execution_group(const struct kernel_object *object);
/* Snapshot a forwarded target while checking destination containment. Actual
 * CALL admission checks its captured target too, including after RETARGET. */
bool launcher_grant_allowed(struct execution_group *group, struct kernel_object *object);

/* Current user task, IF=0, checked protocol tag. Captures input on caller CPU;
 * The retained caller owns preparation; file/device effects keep their fixed
 * owners. May sleep, with no held spinlocks. */
struct syscall_result launcher_call(struct kernel_object *object, uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
