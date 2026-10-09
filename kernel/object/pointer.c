#include <kernel/object/clipboard.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/display.h>
#include <kernel/object/pointer.h>
#include <kernel/panic.h>
#include <kernel/pointer.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user_memory.h>

static void lock_pointer(struct pointer_object *pointer)
{
  while (atomic_exchange_explicit(&pointer->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_pointer(struct pointer_object *pointer)
{
  atomic_store_explicit(&pointer->locked, false, memory_order_release);
}

bool pointer_owned(struct pointer_object *pointer, struct process *process)
{
  uint64_t flags = cpu_save_interrupts();
  lock_pointer(pointer);
  bool owned = pointer->owner == process;
  unlock_pointer(pointer);
  cpu_restore_interrupts(flags);
  return owned;
}

uint64_t pointer_ready(struct pointer_object *pointer, struct process *process)
{
  uint64_t flags = cpu_save_interrupts();
  lock_pointer(pointer);
  uint64_t ready = 0;
  if (pointer->owner != process) {
    ready = WAIT_ERROR;
  } else if (pointer->count) {
    ready = WAIT_READABLE;
  }
  unlock_pointer(pointer);
  cpu_restore_interrupts(flags);
  return ready;
}

static void destroy_pointer(struct kernel_object *object)
{
  struct pointer_object *pointer = (struct pointer_object *)object;
  KASSERT(!pointer->owner && !pointer->reader && !pointer->image);
  kfree(pointer);
}

static struct pointer_object *create_pointer(struct space *space, enum object_type type)
{
  KASSERT(arch_cpu_index() == 0);
  struct pointer_object *pointer = kmalloc(sizeof(*pointer));
  if (!pointer) {
    return NULL;
  }
  *pointer = (struct pointer_object){.space = space};
  atomic_init(&pointer->locked, false);
  object_init(&pointer->object, type, destroy_pointer);
  return pointer;
}

struct pointer_object *pointer_create(struct space *space)
{
  return create_pointer(space, OBJECT_POINTER);
}

struct pointer_object *terminal_pointer_create(struct space *space)
{
  return create_pointer(space, OBJECT_TERMINAL_POINTER);
}

/* Lock held. Detach before waking a task that can immediately resume on an AP. */
static void queue_event(struct pointer_object *pointer, struct pointer_event event)
{
  KASSERT(pointer->count < POINTER_EVENT_CAPACITY);
  size_t tail = (pointer->head + pointer->count) % POINTER_EVENT_CAPACITY;
  event.flags = (pointer->focused ? POINTER_EVENT_FOCUSED : 0) |
      (pointer->relative ? POINTER_EVENT_LOCKED : 0);
  pointer->events[tail] = event;
  ++pointer->count;
  struct task_wait *reader = pointer->reader;
  pointer->reader = NULL;
  if (reader) {
    task_wait_wake(reader);
  }
}

static void reset_buttons(struct pointer_object *pointer)
{
  if (pointer_is_terminal(pointer)) {
    clipboard_space_cancel(pointer->space);
  }
  pointer->head = pointer->count = 0;
  pointer->accepted = 0;
}

void pointer_set_lock(struct pointer_object *pointer, bool relative)
{
  KASSERT(arch_cpu_index() == 0);
  lock_pointer(pointer);
  pointer->relative = relative;
  reset_buttons(pointer);
  if (pointer->owner) {
    queue_event(pointer, pointer_position_event(pointer, POINTER_LOCK_CHANGED));
  }
  unlock_pointer(pointer);
  readiness_notify();
}

void pointer_reset_input(struct pointer_object *pointer, uint32_t type)
{
  KASSERT(arch_cpu_index() == 0);
  lock_pointer(pointer);
  reset_buttons(pointer);
  if (pointer->owner) {
    queue_event(pointer, pointer_position_event(pointer, type));
  }
  unlock_pointer(pointer);
  readiness_notify();
}

void pointer_focus(struct pointer_object *pointer, bool focused)
{
  KASSERT(arch_cpu_index() == 0);
  lock_pointer(pointer);
  bool changed = pointer->focused != focused;
  if (changed) {
    pointer->focused = focused;
    reset_buttons(pointer);
    if (pointer->owner) {
      queue_event(pointer, pointer_position_event(pointer,
          focused ? POINTER_FOCUS_GAINED : POINTER_FOCUS_LOST));
    }
  }
  unlock_pointer(pointer);
  if (changed) {
    readiness_notify();
  }
}

void pointer_queue_state(struct pointer_object *pointer, uint32_t type)
{
  KASSERT(arch_cpu_index() == 0);
  lock_pointer(pointer);
  bool owned = pointer->owner != NULL;
  if (owned) {
    if (type == POINTER_LEAVE) {
      pointer->accepted = 0;
    }
    if (pointer->count == POINTER_EVENT_CAPACITY) {
      reset_buttons(pointer);
      queue_event(pointer, pointer_position_event(pointer, POINTER_STATE_RESET));
    }
    queue_event(pointer, pointer_position_event(pointer, type));
  }
  unlock_pointer(pointer);
  if (owned) {
    readiness_notify();
  }
}

static int32_t saturating_add(int32_t total, int32_t delta)
{
  if (delta > 0 && total > INT32_MAX - delta) {
    return INT32_MAX;
  }
  if (delta < 0 && total < INT32_MIN - delta) {
    return INT32_MIN;
  }
  return total + delta;
}

void pointer_queue_input(struct pointer_object *pointer, struct pointer_event event,
    uint32_t pressed, bool buttons)
{
  KASSERT(arch_cpu_index() == 0);
  lock_pointer(pointer);
  if (!pointer->owner || !pointer->focused) {
    unlock_pointer(pointer);
    return;
  }
  if (buttons) {
    pointer->accepted = (pointer->accepted & event.buttons) | (pressed & event.buttons);
  }
  event.buttons = pointer->accepted;
  if (pointer->count < POINTER_EVENT_CAPACITY) {
    queue_event(pointer, event);
  } else {
    size_t tail = (pointer->head + pointer->count - 1) % POINTER_EVENT_CAPACITY;
    struct pointer_event *last = &pointer->events[tail];
    if (last->type == POINTER_INPUT && last->buttons == event.buttons &&
        last->generation == event.generation && last->mapping_identity == event.mapping_identity) {
      last->x = event.x;
      last->y = event.y;
      last->wheel = saturating_add(last->wheel, event.wheel);
      last->dx = saturating_add(last->dx, event.dx);
      last->dy = saturating_add(last->dy, event.dy);
    } else {
      reset_buttons(pointer);
      queue_event(pointer, pointer_position_event(pointer, POINTER_STATE_RESET));
    }
  }
  unlock_pointer(pointer);
  readiness_notify();
}

void pointer_end_session(struct pointer_object *pointer)
{
  if (pointer_is_terminal(pointer)) {
    clipboard_space_cancel(pointer->space);
  }
  KASSERT(arch_cpu_index() == 0);
  lock_pointer(pointer);
  /* One task per process; the owner cannot release/exit while READ sleeps. */
  KASSERT(!pointer->reader);
  pointer->owner = NULL;
  reset_buttons(pointer);
  unlock_pointer(pointer);
  pointer_subscription_ended(pointer);
  pointer_image_release(pointer->image);
  pointer->image = NULL;
  pointer->hidden = false;
  readiness_notify();
}

void pointer_process_exit(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  struct pointer_object *pointer = process->space->pointer;
  if (pointer->owner == process) {
    pointer_end_session(pointer);
  }
  pointer = process->space->terminal_pointer;
  if (pointer->owner == process) {
    pointer_end_session(pointer);
  }
}

void pointer_request_execute(struct pointer_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->process && request->pointer);
  struct pointer_object *pointer = request->pointer;
  struct process *process = request->process;
  enum call_status status = CALL_OK;
  bool terminal = pointer_is_terminal(pointer);
  if (process->space != pointer->space ||
      (!terminal && pointer->space->display->owner != process)) {
    status = CALL_DENIED;
  } else if (request->operation == POINTER_ACQUIRE) {
    if (!terminal && !space_pointer_input_available()) {
      status = CALL_UNAVAILABLE;
    } else if (pointer->owner) {
      status = CALL_BUSY;
    } else if (terminal && pointer->view_identity == UINT64_MAX) {
      status = CALL_LIMIT;
    } else {
      lock_pointer(pointer);
      pointer->owner = process;
      if (terminal) {
        ++pointer->view_identity;
      }
      pointer->focused = pointer_surface_focused(pointer);
      reset_buttons(pointer);
      queue_event(pointer, pointer_position_event(pointer,
          pointer->focused ? POINTER_FOCUS_GAINED : POINTER_FOCUS_LOST));
      unlock_pointer(pointer);
      readiness_notify();
      pointer_subscription_started(pointer);
    }
  } else if (pointer->owner != process) {
    status = CALL_DENIED;
  } else if (request->operation == POINTER_RELEASE) {
    pointer_end_session(pointer);
  } else if (request->operation == TERMINAL_POINTER_CANCEL_CLIPBOARD) {
    clipboard_space_cancel(pointer->space);
  } else if (request->operation == POINTER_GEOMETRY) {
    if (terminal) {
      struct tty *tty = pointer->space->tty;
      request->data.terminal_geometry = (struct terminal_pointer_geometry){
        .surface = pointer_surface_geometry(pointer),
        .columns = tty->width, .rows = tty->height,
        .cell_width = tty->font->width, .cell_height = tty->font->height,
      };
    } else {
      request->data.geometry = pointer_surface_geometry(pointer);
    }
  } else if (request->operation == TERMINAL_POINTER_VIEW_CHANGED) {
    /* Driver records still preceding this boundary belong to the old view. */
    space_pointer_sync_input();
    struct pointer_geometry geometry = pointer_surface_geometry(pointer);
    if (request->data.view.generation != geometry.generation ||
        request->data.view.mapping_identity != geometry.mapping_identity) {
      status = CALL_BUSY;
    } else if (pointer->view_identity == UINT64_MAX) {
      status = CALL_LIMIT;
    } else {
      ++pointer->view_identity;
      pointer_terminal_view_changed(pointer);
      struct tty *tty = pointer->space->tty;
      request->data.terminal_geometry = (struct terminal_pointer_geometry){
        .surface = pointer_surface_geometry(pointer),
        .columns = tty->width, .rows = tty->height,
        .cell_width = tty->font->width, .cell_height = tty->font->height,
      };
    }
  } else if (request->operation == POINTER_STATE) {
    request->data.flags = (pointer->focused ? POINTER_EVENT_FOCUSED : 0) |
        (pointer->relative ? POINTER_EVENT_LOCKED : 0);
  } else if (request->operation == POINTER_LOCK) {
    status = pointer_surface_lock(pointer);
  } else if (request->operation == POINTER_UNLOCK) {
    pointer_surface_unlock(pointer, false);
  } else if (request->operation == POINTER_VISIBILITY) {
    pointer->hidden = !request->data.visible;
  } else if (request->operation == POINTER_DEFAULT_IMAGE) {
    pointer_image_release(pointer->image);
    pointer->image = NULL;
  } else if (request->operation == POINTER_WARP) {
    status = pointer_surface_warp(pointer, &request->data.warp);
  } else if (request->stage == POINTER_REQUEST_IMAGE_PREPARE) {
    const struct pointer_image_request *image = &request->data.image;
    size_t bytes = (size_t)image->width * image->height * 4;
    struct pointer_image *candidate = kmalloc(sizeof(*candidate) + bytes);
    if (!candidate) {
      status = CALL_NO_MEMORY;
    } else {
      *candidate = (struct pointer_image){.references = 1,
        .width = image->width, .height = image->height,
        .hotspot_x = image->hotspot_x, .hotspot_y = image->hotspot_y};
      request->candidate = candidate;
    }
  } else {
    KASSERT(request->stage == POINTER_REQUEST_IMAGE_COMMIT && request->candidate);
    pointer_image_release(pointer->image);
    pointer->image = request->candidate;
    request->candidate = NULL;
  }
  if (request->stage == POINTER_REQUEST_IMAGE_COMMIT && request->candidate) {
    pointer_image_release(request->candidate);
    request->candidate = NULL;
  }
  request->result = status;
  request->process = NULL;
  request->pointer = NULL;
}

static enum call_status command_pointer(struct pointer_request *command)
{
  struct pointer_request *request =
      (struct pointer_request *)bsp_request_prepare(BSP_SERVICE_POINTER);
  request->process = process_current();
  request->pointer = command->pointer;
  request->operation = command->operation;
  request->stage = command->stage;
  request->data = command->data;
  request->candidate = command->candidate;
  bsp_request_submit_and_wait(&request->request);
  command->data = request->data;
  command->candidate = request->candidate;
  enum call_status status = request->result;
  bsp_request_release(&request->request);
  return status;
}

static struct syscall_result read_pointer(struct pointer_object *pointer,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  uint64_t flags;
  struct pointer_event reply;
  if (request_size != sizeof(flags) || reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&flags, request_address, sizeof(flags)) ||
      !user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (flags & ~POINTER_READ_POLL) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct process *process = process_current();
  lock_pointer(pointer);
  if (pointer->owner != process) {
    unlock_pointer(pointer);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  while (!pointer->count) {
    if (task_stop_requested()) {
      unlock_pointer(pointer);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
    if (flags & POINTER_READ_POLL) {
      unlock_pointer(pointer);
      return (struct syscall_result){CALL_TIMED_OUT, 0};
    }
    struct task_wait *wait = task_wait_prepare();
    KASSERT(!pointer->reader);
    pointer->reader = wait;
    unlock_pointer(pointer);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_pointer(pointer);
    if (pointer->reader == wait) {
      pointer->reader = NULL;
    }
    KASSERT(!pointer->reader && pointer->owner == process);
    if (!resumed || task_stop_requested()) {
      unlock_pointer(pointer);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
  }
  reply = pointer->events[pointer->head];
  pointer->head = (pointer->head + 1) % POINTER_EVENT_CAPACITY;
  --pointer->count;
  unlock_pointer(pointer);
  readiness_notify();
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result pointer_call(struct pointer_object *pointer, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  bool terminal = pointer_is_terminal(pointer);
  if (operation < POINTER_ACQUIRE ||
      operation > (terminal ? TERMINAL_POINTER_CANCEL_CLIPBOARD : POINTER_STATE) ||
      (terminal && (operation == POINTER_WARP || operation == POINTER_LOCK ||
                    operation == POINTER_UNLOCK))) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  uint64_t required = terminal ? TERMINAL_POINTER_RIGHT_CONTROL : POINTER_RIGHT_INPUT;
  if (!(rights & required) || process_current()->space != pointer->space) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == TERMINAL_POINTER_CLIPBOARD_REFUSE) {
    return clipboard_controller_refuse_call(request_address, request_size);
  }
  if (operation == POINTER_READ) {
    return read_pointer(pointer, request_address, request_size, reply_address, reply_capacity);
  }
  struct pointer_request command = {.pointer = pointer, .operation = operation};
  size_t payload = 0;
  void *destination = NULL;
  if (operation == POINTER_SET_IMAGE) {
    payload = sizeof(struct pointer_image_request) - sizeof(struct message_header);
    destination = &command.data.image.address;
  } else if (operation == POINTER_VISIBILITY) {
    payload = sizeof(uint64_t);
    destination = &command.data.visible;
  } else if (operation == POINTER_WARP) {
    payload = sizeof(struct pointer_warp_request) - sizeof(struct message_header);
    destination = &command.data.warp.x;
  }
  if (operation == TERMINAL_POINTER_VIEW_CHANGED) {
    payload = sizeof(struct terminal_pointer_view_request) - sizeof(struct message_header);
    destination = &command.data.view.generation;
  }
  size_t reply_size = operation == TERMINAL_POINTER_VIEW_CHANGED ? sizeof(struct terminal_pointer_geometry) :
      operation == POINTER_GEOMETRY ? (terminal ? sizeof(struct terminal_pointer_geometry) :
          sizeof(struct pointer_geometry)) :
      operation == POINTER_STATE ? sizeof(uint64_t) : 0;
  if (request_size != payload || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if ((payload && !copy_from_user(destination, request_address, payload)) ||
      (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == POINTER_VISIBILITY && command.data.visible > 1) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (operation == POINTER_SET_IMAGE) {
    struct pointer_image_request image = command.data.image;
    if (!image.width || !image.height || image.width > POINTER_IMAGE_MAX ||
        image.height > POINTER_IMAGE_MAX || image.hotspot_x >= image.width ||
        image.hotspot_y >= image.height) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    size_t bytes = (size_t)image.width * image.height * 4;
    if (!user_buffer_check(image.address, bytes, USER_BUFFER_READ)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    command.stage = POINTER_REQUEST_IMAGE_PREPARE;
    enum call_status status = command_pointer(&command);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
    KASSERT(command.candidate);
    KASSERT(copy_from_user(command.candidate->pixels, image.address, bytes));
    command.stage = POINTER_REQUEST_IMAGE_COMMIT;
  }
  enum call_status status = command_pointer(&command);
  if (status == CALL_OK && (operation == POINTER_GEOMETRY ||
      operation == TERMINAL_POINTER_VIEW_CHANGED)) {
    const void *geometry = terminal ? (const void *)&command.data.terminal_geometry :
        (const void *)&command.data.geometry;
    KASSERT(copy_to_user(reply_address, geometry, reply_size));
    return (struct syscall_result){CALL_OK, reply_size};
  }
  if (status == CALL_OK && operation == POINTER_STATE) {
    KASSERT(copy_to_user(reply_address, &command.data.flags, sizeof(command.data.flags)));
    return (struct syscall_result){CALL_OK, sizeof(command.data.flags)};
  }
  return (struct syscall_result){status, 0};
}

void terminal_pointer_clipboard_action(struct pointer_object *pointer, uint64_t action_id,
    uint64_t operation, uint64_t layer)
{
  lock_pointer(pointer);
  if (pointer->count == POINTER_EVENT_CAPACITY) {
    reset_buttons(pointer);
    queue_event(pointer, pointer_position_event(pointer, POINTER_STATE_RESET));
  } else {
    struct pointer_event event = pointer_position_event(pointer, TERMINAL_POINTER_CLIPBOARD_ACTION);
    event.action_id = action_id;
    event.clipboard_operation = operation;
    event.clipboard_layer = layer;
    queue_event(pointer, event);
  }
  unlock_pointer(pointer);
  readiness_notify();
}
