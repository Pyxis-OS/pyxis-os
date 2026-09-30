#include <arch/clock.h>
#include <abi/tcp.h>
#include <abi/execution_group.h>
#include <abi/process.h>
#include <abi/terminal.h>
#include <kernel/object/object.h>
#include <kernel/process.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <kernel/user/wait.h>

static enum call_status interest_authority(const struct kernel_object *object,
    uint64_t rights, uint64_t events)
{
  if (!events) {
    return CALL_BAD_REQUEST;
  }
  uint64_t required = 0;
  if (object->type == OBJECT_TCP) {
    if (events & ~(WAIT_READABLE | WAIT_WRITABLE | WAIT_PEER_FIN | WAIT_WRITE_CLOSED)) {
      return CALL_BAD_REQUEST;
    }
    if (events & (WAIT_READABLE | WAIT_PEER_FIN)) {
      required |= TCP_RIGHT_READ;
    }
    if (events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED)) {
      required |= TCP_RIGHT_WRITE;
    }
  } else if (object->type == OBJECT_TCP_LISTENER) {
    if (events & ~(WAIT_ACCEPTABLE | WAIT_CLOSED)) {
      return CALL_BAD_REQUEST;
    }
    required = TCP_LISTENER_RIGHT_ACCEPT;
  } else if (object->type == OBJECT_TERMINAL_ATTACHMENT) {
    if (events & ~(WAIT_READABLE | WAIT_WRITABLE | WAIT_PEER_FIN | WAIT_WRITE_CLOSED)) {
      return CALL_BAD_REQUEST;
    }
    if (events & (WAIT_READABLE | WAIT_PEER_FIN)) {
      required |= TERMINAL_RIGHT_DRAIN;
    }
    if (events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED)) {
      required |= TERMINAL_RIGHT_INJECT;
    }
  } else if (object->type == OBJECT_PROCESS_CONTROL) {
    if (events != WAIT_COMPLETE) {
      return CALL_BAD_REQUEST;
    }
    required = PROCESS_RIGHT_WAIT;
  } else if (object->type == OBJECT_EXECUTION_GROUP) {
    if (events != WAIT_COMPLETE) {
      return CALL_BAD_REQUEST;
    }
    required = EXECUTION_GROUP_RIGHT_WAIT;
  } else {
    return CALL_BAD_REQUEST;
  }
  return (rights & required) == required ? CALL_OK : CALL_DENIED;
}

struct syscall_result user_wait_many(uintptr_t interests, uint64_t count,
    uint64_t deadline, uintptr_t output)
{
  uint64_t now = arch_monotonic_ns();
  if (!count || count > WAIT_MAX_INTERESTS ||
      (deadline > now && deadline - now > WAIT_MAX_WAIT_NS)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct wait_interest input[WAIT_MAX_INTERESTS];
  size_t output_size = count * sizeof(uint64_t);
  if (!copy_from_user(input, interests, count * sizeof(*input)) ||
      !user_buffer_check(output, output_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct kernel_object *objects[WAIT_MAX_INTERESTS];
  size_t retained = 0;
  enum call_status status = CALL_OK;
  for (size_t i = 0; i < count; ++i) {
    uint64_t rights;
    if (capability_resolve(&process_current()->capabilities, input[i].handle,
          0, 0, &objects[i], &rights, NULL) != CAP_OK) {
      status = CALL_BAD_HANDLE;
      break;
    }
    status = interest_authority(objects[i], rights, input[i].events);
    if (status != CALL_OK) {
      break;
    }
    if (!object_retain(objects[i])) {
      status = CALL_LIMIT;
      break;
    }
    ++retained;
  }
  if (status != CALL_OK) {
    for (size_t i = 0; i < retained; ++i) {
      object_release(objects[i]);
    }
    return (struct syscall_result){status, 0};
  }

  struct readiness_request *request =
      (struct readiness_request *)bsp_request_prepare(BSP_SERVICE_READINESS);
  request->count = count;
  request->deadline = deadline;
  for (size_t i = 0; i < count; ++i) {
    request->interests[i] = (struct readiness_interest){objects[i], input[i].events, 0};
  }
  bsp_request_submit_and_wait(&request->request);
  status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : request->status;
  if (status == CALL_OK) {
    uint64_t events[WAIT_MAX_INTERESTS];
    for (size_t i = 0; i < count; ++i) {
      KASSERT(!request->interests[i].object);
      events[i] = request->interests[i].ready;
    }
    KASSERT(copy_to_user(output, events, output_size));
  }
  bsp_request_release(&request->request);
  return (struct syscall_result){status, status == CALL_OK ? output_size : 0};
}
