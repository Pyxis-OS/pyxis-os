#include <arch/clock.h>
#include <abi/console.h>
#include <abi/display.h>
#include <abi/keyboard.h>
#include <abi/pointer.h>
#include <abi/tcp.h>
#include <abi/execution_group.h>
#include <abi/process.h>
#include <abi/terminal.h>
#include <kernel/object/object.h>
#include <kernel/object/console.h>
#include <kernel/object/display.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/audio.h>
#include <kernel/object/pointer.h>
#include <kernel/object/pipe.h>
#include <kernel/object/endpoint.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <kernel/user/wait.h>

static enum call_status interest_authority(const struct kernel_object *object,
    struct process *caller, uint64_t rights, uint64_t transport, uint64_t events)
{
  if (!events) {
    return CALL_BAD_REQUEST;
  }
  uint64_t required = 0;
  if (object->type == OBJECT_ENDPOINT_RECEIVER) {
    if (events & ~(WAIT_READABLE | WAIT_CLOSED)) {
      return CALL_BAD_REQUEST;
    }
    const struct endpoint *receiver = (const struct endpoint *)object;
    if (receiver->owner != caller || !(transport & HANDLE_TRANSPORT_RECEIVE)) {
      return CALL_DENIED;
    }
  } else if (object->type == OBJECT_TCP) {
    if (events & ~(WAIT_READABLE | WAIT_WRITABLE | WAIT_PEER_FIN | WAIT_WRITE_CLOSED)) {
      return CALL_BAD_REQUEST;
    }
    if (events & (WAIT_READABLE | WAIT_PEER_FIN)) {
      required |= TCP_RIGHT_READ;
    }
    if (events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED)) {
      required |= TCP_RIGHT_WRITE;
    }
  } else if (object->type == OBJECT_PIPE) {
    const struct pipe_end *end = (const struct pipe_end *)object;
    uint64_t allowed = end->reader ? WAIT_READABLE | WAIT_PEER_FIN :
        WAIT_WRITABLE | WAIT_WRITE_CLOSED;
    if (events & ~allowed) {
      return CALL_BAD_REQUEST;
    }
    required = end->reader ? PIPE_RIGHT_READ : PIPE_RIGHT_WRITE;
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
  } else if (object->type == OBJECT_CONSOLE || object->type == OBJECT_TERMINAL_INPUT) {
    uint64_t allowed = WAIT_READABLE | WAIT_INTERRUPT | WAIT_RESIZED;
    if (object->type == OBJECT_TERMINAL_INPUT) {
      allowed |= WAIT_PEER_FIN;
    } else {
      allowed |= WAIT_WRITABLE | WAIT_WRITE_CLOSED;
    }
    if (events & ~allowed) {
      return CALL_BAD_REQUEST;
    }
    if (events & (WAIT_READABLE | WAIT_PEER_FIN)) {
      required |= CONSOLE_RIGHT_READ;
    }
    if (events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED)) {
      required |= CONSOLE_RIGHT_WRITE;
    }
    if (events & WAIT_INTERRUPT) {
      required |= CONSOLE_RIGHT_ARMED;
    }
    if ((events & WAIT_RESIZED) && !(rights & CONSOLE_RIGHTS)) {
      return CALL_DENIED;
    }
  } else if (object->type == OBJECT_TERMINAL_OUTPUT) {
    if (events & ~(WAIT_WRITABLE | WAIT_WRITE_CLOSED | WAIT_RESIZED)) {
      return CALL_BAD_REQUEST;
    }
    required = CONSOLE_RIGHT_WRITE;
  } else if (object->type == OBJECT_DISPLAY) {
    if (events != WAIT_RESIZED) {
      return CALL_BAD_REQUEST;
    }
    if (((const struct display_object *)object)->space != caller->space) {
      return CALL_DENIED;
    }
    required = DISPLAY_RIGHT_DRAW;
  } else if (object->type == OBJECT_KEYBOARD) {
    if (events != WAIT_READABLE) {
      return CALL_BAD_REQUEST;
    }
    struct keyboard_object *keyboard = (struct keyboard_object *)object;
    if (keyboard->space != caller->space || !keyboard_owned(keyboard, caller)) {
      return CALL_DENIED;
    }
    required = KEYBOARD_RIGHT_INPUT;
  } else if (object->type == OBJECT_AUDIO) {
    if (events != WAIT_WRITABLE) {
      return CALL_BAD_REQUEST;
    }
    struct audio_object *audio = (struct audio_object *)object;
    if (audio->space != caller->space || !audio_owned(audio, caller)) {
      return CALL_DENIED;
    }
    required = AUDIO_RIGHT_PLAYBACK;
  } else if (object->type == OBJECT_POINTER || object->type == OBJECT_TERMINAL_POINTER) {
    if (events != WAIT_READABLE) {
      return CALL_BAD_REQUEST;
    }
    struct pointer_object *pointer = (struct pointer_object *)object;
    if (pointer->space != caller->space || !pointer_owned(pointer, caller)) {
      return CALL_DENIED;
    }
    required = object->type == OBJECT_POINTER ? POINTER_RIGHT_INPUT :
        TERMINAL_POINTER_RIGHT_CONTROL;
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
  struct process *caller = process_current();
  size_t retained = 0;
  enum call_status status = CALL_OK;
  for (size_t i = 0; i < count; ++i) {
    uint64_t rights, transport;
    if (capability_resolve(&caller->capabilities, input[i].handle,
          0, 0, &objects[i], &rights, &transport) != CAP_OK) {
      status = CALL_BAD_HANDLE;
      break;
    }
    status = interest_authority(objects[i], caller, rights, transport, input[i].events);
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
  request->caller = caller;
  for (size_t i = 0; i < count; ++i) {
    request->interests[i] = (struct readiness_interest){
      .object = objects[i], .events = input[i].events,
      .observed_generation = input[i].observed_generation,
    };
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
