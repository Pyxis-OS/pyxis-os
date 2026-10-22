#include <kernel/object/capability.h>
#include <kernel/object/process.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static enum call_status capability_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK:
    return CALL_OK;
  case CAP_BAD_HANDLE:
    return CALL_BAD_HANDLE;
  case CAP_DENIED:
    return CALL_DENIED;
  case CAP_INVALID:
    return CALL_BAD_REQUEST;
  case CAP_NO_MEMORY:
    return CALL_NO_MEMORY;
  case CAP_LIMIT:
  case CAP_FULL:
    return CALL_LIMIT;
  case CAP_NOT_FOUND:
    return CALL_NOT_FOUND;
  }
  KASSERT(false);
}

struct syscall_result process_interrupt_create_call(uintptr_t reply_address, uint64_t capacity)
{
  if (capacity < sizeof(struct process_interrupt_create_reply)) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct process_interrupt_create_reply),
      USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  enum capability_result acquired = capability_request_reservation(1, &reservation, &slot);
  if (acquired != CAP_OK) {
    return (struct syscall_result){capability_status(acquired), 0};
  }
  struct process *caller = process_current();
  struct process_interrupt_create_request *request =
      (void *)bsp_request_prepare(BSP_SERVICE_PROCESS_INTERRUPT_CREATE);
  request->caller = caller;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  struct kernel_object *created = request->created;
  request->created = NULL;
  bsp_request_release(&request->request);

  struct process_interrupt_create_reply reply = {0};
  if (status == CALL_OK) {
    KASSERT(created);
    struct capability_grant grant = {0};
    acquired = capability_grant_retain(created, PROCESS_INTERRUPT_RIGHT_RECEIVE, 0, &grant);
    if (acquired == CAP_OK) {
      acquired = capability_validate_grants(reservation.table, &grant, 1);
    }
    status = capability_status(acquired);
    if (status == CALL_OK) {
      capability_install_reserved(&reservation, &slot, &grant, 1, &reply.handle);
    }
    capability_grant_release(&grant);
  }
  capability_reservation_release(&reservation, &slot);

  if (status == CALL_OK) {
    if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1)) {
      status = CALL_BAD_BUFFER;
    } else {
      /* A sibling may close the copied handle before this publication.
       * The creator's storage reference does not keep RECEIVE ownership alive. */
      status = process_interrupt_publish(created, caller);
    }
  }
  if (status != CALL_OK) {
    if (created) {
      /* A sibling may also have copied the installed grant. Failed creation
       * closes that unpublished receiver even when such copies survive. */
      process_interrupt_abandon(created);
    }
    capability_close_returned(&caller->capabilities, &reply.handle, 1);
  }
  if (created) {
    object_release(created);
  }
  return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
}
