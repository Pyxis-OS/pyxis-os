#include <abi/readiness_set.h>
#include <arch/clock.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user/readiness_set.h>
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

struct syscall_result
readiness_set_create_call(uintptr_t reply_address, uint64_t reply_capacity)
{
  if (reply_capacity < sizeof(struct readiness_set_create_reply)) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct readiness_set_create_reply),
      USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  enum capability_result acquired = capability_request_reservation(1, &reservation, &slot);
  if (acquired != CAP_OK) {
    return (struct syscall_result){capability_status(acquired), 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }

  struct readiness_set_request *request =
      (void *)bsp_request_prepare(BSP_SERVICE_READINESS_SET);
  request->caller = process_current();
  request->operation = READINESS_CREATE;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  struct kernel_object *created = request->created;
  request->created = NULL;
  bsp_request_release(&request->request);

  struct readiness_set_create_reply reply = {0};
  bool installed = false;
  if (status == CALL_OK) {
    KASSERT(created);
    struct capability_grant grant = {0};
    acquired = capability_grant_retain(created, READINESS_SET_RIGHTS, 0, &grant);
    if (acquired == CAP_OK) {
      acquired = capability_validate_grants(reservation.table, &grant, 1);
    }
    status = capability_status(acquired);
    if (status == CALL_OK) {
      capability_install_reserved(&reservation, &slot, &grant, 1, &reply.handle);
      installed = true;
    }
    capability_grant_release(&grant);
  }
  capability_reservation_release(&reservation, &slot);
  if (created) {
    if (!installed) {
      readiness_set_abandon(created);
    }
    object_release(created);
  }
  if (status == CALL_OK && !copy_returned_handles_to_user(reply_address, &reply,
      sizeof(reply), &reply.handle, 1)) {
    status = CALL_BAD_BUFFER;
  }
  return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
}

static void finish_request(struct process *caller, struct readiness_set *set,
    struct readiness_registration *prepared, uint64_t operation, bool copied)
{
  struct readiness_set_request *request =
      (void *)bsp_request_prepare(BSP_SERVICE_READINESS_SET);
  request->caller = caller;
  request->set = set;
  request->prepared = prepared;
  request->operation = READINESS_FINISH;
  request->finish_operation = operation;
  request->copied = copied;
  /* Cleanup must join even after revocation of the outer forwarded call. */
  request->request.operation = NULL;
  bsp_request_submit_and_wait(&request->request);
  bsp_request_release(&request->request);
}

struct syscall_result
readiness_set_call(struct kernel_object *object, uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  union {
    struct message_header header;
    struct readiness_set_register_request registration;
    struct readiness_set_update_request update;
    struct readiness_set_remove_request remove;
    struct readiness_set_wait_request wait;
  } input;
  size_t input_size, reply_size;
  uint64_t required;
  switch (operation) {
  case READINESS_SET_REGISTER:
    input_size = sizeof(input.registration);
    reply_size = sizeof(struct readiness_set_registration);
    required = READINESS_SET_RIGHT_CONTROL;
    break;
  case READINESS_SET_UPDATE:
    input_size = sizeof(input.update);
    reply_size = sizeof(struct readiness_set_registration);
    required = READINESS_SET_RIGHT_CONTROL;
    break;
  case READINESS_SET_REMOVE:
    input_size = sizeof(input.remove);
    reply_size = 0;
    required = READINESS_SET_RIGHT_CONTROL;
    break;
  case READINESS_SET_WAIT:
    input_size = sizeof(input.wait);
    reply_size = sizeof(struct readiness_set_wait_reply);
    required = READINESS_SET_RIGHT_WAIT;
    break;
  case READINESS_SET_WAKE:
    input_size = sizeof(input.header);
    reply_size = 0;
    required = READINESS_SET_RIGHT_CONTROL;
    break;
  case READINESS_SET_INFO:
    input_size = sizeof(input.header);
    reply_size = sizeof(struct readiness_set_info);
    required = READINESS_SET_RIGHT_CONTROL;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  input_size -= sizeof(struct message_header);
  struct process *caller = process_current();
  if (!(rights & required) || !readiness_set_owned(object, caller)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size != input_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }
  if (!copy_from_user((uint8_t *)&input + sizeof(struct message_header),
      request_address, input_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == READINESS_SET_REGISTER &&
      (!input.registration.allowed_events ||
      (input.registration.active_events & ~input.registration.allowed_events))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (operation == READINESS_SET_WAIT) {
    uint64_t now = arch_monotonic_ns();
    if (!input.wait.capacity ||
        (input.wait.deadline > now && input.wait.deadline - now > WAIT_MAX_WAIT_NS)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (input.wait.capacity > READINESS_SET_BATCH_MAX) {
      input.wait.capacity = READINESS_SET_BATCH_MAX;
    }
    if (!user_buffer_check(input.wait.events_address,
        input.wait.capacity * sizeof(struct readiness_set_event), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  }
  if (!task_operation_start()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (task_stop_requested()) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }

  struct capability_reference target = {0};
  if (operation == READINESS_SET_REGISTER) {
    enum capability_result acquired = capability_acquire(&caller->capabilities,
        input.registration.target, 0, 0, &target);
    if (acquired != CAP_OK) {
      return (struct syscall_result){capability_status(acquired), 0};
    }
  }
  struct readiness_set_request *request =
      (void *)bsp_request_prepare(BSP_SERVICE_READINESS_SET);
  request->caller = caller;
  request->set = (struct readiness_set *)object;
  request->operation = operation;
  request->target = target;
  if (operation == READINESS_SET_REGISTER) {
    request->allowed_events = input.registration.allowed_events;
    request->active_events = input.registration.active_events;
    request->observed_generation = input.registration.observed_generation;
    request->tag = input.registration.tag;
  } else if (operation == READINESS_SET_UPDATE) {
    request->key = input.update.registration;
    request->active_events = input.update.active_events;
    request->observed_generation = input.update.observed_generation;
  } else if (operation == READINESS_SET_REMOVE) {
    request->key = input.remove.registration;
  } else if (operation == READINESS_SET_WAIT) {
    request->deadline = input.wait.deadline;
    request->capacity = input.wait.capacity;
    request->events_address = input.wait.events_address;
  }
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  capability_release(&request->target);
  bool staged = status == CALL_OK && (operation == READINESS_SET_REGISTER ||
      operation == READINESS_SET_UPDATE || operation == READINESS_SET_WAIT);
  bool copied = false;
  if (status == CALL_OK && reply_size) {
    struct user_buffer_lease lease;
    if (!task_operation_start()) {
      status = CALL_DENIED;
    } else if (task_stop_requested()) {
      status = CALL_ENDPOINT_CLOSED;
    } else if (!user_buffer_lease_acquire(&lease)) {
      status = CALL_BAD_BUFFER;
    } else {
      if (operation == READINESS_SET_REGISTER || operation == READINESS_SET_UPDATE) {
        if (!user_buffer_check_leased(&lease, reply_address, reply_size, USER_BUFFER_WRITE)) {
          status = CALL_BAD_BUFFER;
        } else {
          status = readiness_set_publish(request, &lease, reply_address, reply_capacity);
        }
        copied = status == CALL_OK;
      } else if (operation == READINESS_SET_WAIT) {
        KASSERT(request->wait_reply.count <= input.wait.capacity);
        status = readiness_set_publish(request, &lease, reply_address, reply_capacity);
        copied = status == CALL_OK;
      } else {
        if (!copy_reply_to_user_leased(&lease, reply_address, &request->info_reply,
            sizeof(request->info_reply), 0, NULL, 0)) {
          status = CALL_BAD_BUFFER;
        }
      }
      user_buffer_lease_release(&lease);
    }
  }
  struct readiness_registration *prepared = request->prepared;
  struct readiness_set *set = request->set;
  request->prepared = NULL;
  bsp_request_release(&request->request);
  if (staged) {
    finish_request(caller, set, prepared, operation, copied);
  }
  return (struct syscall_result){status, status == CALL_OK ? reply_size : 0};
}
