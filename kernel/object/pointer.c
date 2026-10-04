#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mouse.h>
#include <kernel/object/pointer.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

_Static_assert(MOUSE_BUTTON_LEFT == POINTER_BUTTON_LEFT &&
               MOUSE_BUTTON_RIGHT == POINTER_BUTTON_RIGHT &&
               MOUSE_BUTTON_MIDDLE == POINTER_BUTTON_MIDDLE,
               "device and session button bits match");

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

static void destroy_pointer(struct kernel_object *object)
{
  struct pointer_object *pointer = (struct pointer_object *)object;
  KASSERT(!pointer->owner && !pointer->reader);
  kfree(pointer);
}

struct pointer_object *pointer_create(struct space *space, bool focused)
{
  KASSERT(arch_cpu_index() == 0);
  struct pointer_object *pointer = kmalloc(sizeof(*pointer));
  if (!pointer) {
    return NULL;
  }
  *pointer = (struct pointer_object){.space = space, .focused = focused};
  atomic_init(&pointer->locked, false);
  object_init(&pointer->object, OBJECT_POINTER, destroy_pointer);
  return pointer;
}

/* Under the pointer lock. Detach before wake; the task can resume on an AP
 * immediately. Neither queue publication nor notification touches user memory. */
static void queue_event(struct pointer_object *pointer, struct pointer_event event)
{
  KASSERT(pointer->count < POINTER_EVENT_CAPACITY);
  size_t tail = (pointer->head + pointer->count) % POINTER_EVENT_CAPACITY;
  event.flags = pointer->focused ? POINTER_EVENT_FOCUSED : 0;
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
  pointer->head = pointer->count = 0;
  pointer->accepted = 0;
}

void pointer_focus(struct pointer_object *pointer, bool focused)
{
  KASSERT(arch_cpu_index() == 0);
  lock_pointer(pointer);
  if (pointer->focused != focused) {
    pointer->focused = focused;
    reset_buttons(pointer);
    if (pointer->owner) {
      queue_event(pointer, (struct pointer_event){
        .type = focused ? POINTER_FOCUS_GAINED : POINTER_FOCUS_LOST,
      });
    }
  }
  unlock_pointer(pointer);
}

void pointer_reset_input(struct pointer_object *pointer)
{
  KASSERT(arch_cpu_index() == 0);
  lock_pointer(pointer);
  reset_buttons(pointer);
  if (pointer->owner) {
    queue_event(pointer, (struct pointer_event){.type = POINTER_STATE_RESET});
  }
  unlock_pointer(pointer);
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

void pointer_route_event(struct pointer_object *pointer, const struct mouse_event *event,
                         uint32_t pressed)
{
  KASSERT(arch_cpu_index() == 0 && !event->reset);
  lock_pointer(pointer);
  if (!pointer->focused) {
    unlock_pointer(pointer);
    return;
  }
  /* Releases end acceptance; only presses seen here start it. */
  uint32_t previous = pointer->accepted;
  pointer->accepted = (pointer->accepted & event->buttons) | (pressed & event->buttons);
  bool moved = event->dx || event->dy || event->wheel;
  if (!pointer->owner || (!moved && pointer->accepted == previous)) {
    unlock_pointer(pointer);
    return;
  }

  if (pointer->count < POINTER_EVENT_CAPACITY) {
    queue_event(pointer, (struct pointer_event){
      .dx = event->dx, .dy = event->dy, .wheel = event->wheel,
      .buttons = pointer->accepted, .type = POINTER_INPUT,
    });
    unlock_pointer(pointer);
    return;
  }

  size_t tail = (pointer->head + pointer->count - 1) % POINTER_EVENT_CAPACITY;
  struct pointer_event *last = &pointer->events[tail];
  if (last->type == POINTER_INPUT && last->buttons == pointer->accepted) {
    last->dx = saturating_add(last->dx, event->dx);
    last->dy = saturating_add(last->dy, event->dy);
    last->wheel = saturating_add(last->wheel, event->wheel);
  } else {
    /* A lost button transition invalidates every held button. */
    reset_buttons(pointer);
    queue_event(pointer, (struct pointer_event){.type = POINTER_STATE_RESET});
  }
  unlock_pointer(pointer);
}

static void release_pointer(struct pointer_object *pointer)
{
  /* One task per process: the owner cannot release/exit while its READ sleeps. */
  KASSERT(!pointer->reader);
  pointer->owner = NULL;
  reset_buttons(pointer);
}

void pointer_process_exit(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  struct pointer_object *pointer = process->space->pointer;
  lock_pointer(pointer);
  if (pointer->owner == process) {
    release_pointer(pointer);
  }
  unlock_pointer(pointer);
}

struct syscall_result pointer_call(struct pointer_object *pointer, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != POINTER_ACQUIRE && operation != POINTER_READ && operation != POINTER_RELEASE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  struct process *process = process_current();
  if (!(rights & POINTER_RIGHT_INPUT) || process->space != pointer->space) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  uint64_t flags = 0;
  struct pointer_event reply;
  if (operation == POINTER_READ) {
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
  } else if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  lock_pointer(pointer);
  if (operation == POINTER_ACQUIRE) {
    enum call_status status = CALL_OK;
    if (!mouse_available()) {
      status = CALL_UNAVAILABLE;
    } else if (pointer->owner) {
      status = CALL_BUSY;
    } else {
      pointer->owner = process;
      reset_buttons(pointer);
      queue_event(pointer, (struct pointer_event){
        .type = pointer->focused ? POINTER_FOCUS_GAINED : POINTER_FOCUS_LOST,
      });
    }
    unlock_pointer(pointer);
    return (struct syscall_result){status, 0};
  }
  if (pointer->owner != process) {
    unlock_pointer(pointer);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == POINTER_RELEASE) {
    release_pointer(pointer);
    unlock_pointer(pointer);
    return (struct syscall_result){CALL_OK, 0};
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

  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
