#include <arch/smp.h>
#include <kernel/keyboard.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/console.h>
#include <kernel/object/keyboard.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void lock_keyboard(struct keyboard_object *keyboard)
{
  while (atomic_exchange_explicit(&keyboard->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_keyboard(struct keyboard_object *keyboard)
{
  atomic_store_explicit(&keyboard->locked, false, memory_order_release);
}

static void destroy_keyboard(struct kernel_object *object)
{
  struct keyboard_object *keyboard = (struct keyboard_object *)object;
  KASSERT(!keyboard->owner && !keyboard->reader);
  kfree(keyboard);
}

struct keyboard_object *keyboard_create(struct space *space, bool focused)
{
  KASSERT(arch_cpu_index() == 0);
  struct keyboard_object *keyboard = kmalloc(sizeof(*keyboard));
  if (!keyboard) {
    return NULL;
  }
  *keyboard = (struct keyboard_object){.space = space, .focused = focused};
  atomic_init(&keyboard->locked, false);
  object_init(&keyboard->object, OBJECT_KEYBOARD, destroy_keyboard);
  return keyboard;
}

/* Under the keyboard lock. Detach before wake; the task can resume on an AP
 * immediately. Neither queue publication nor notification touches user memory. */
static void queue_event(struct keyboard_object *keyboard, struct keyboard_event event)
{
  KASSERT(keyboard->count < KEYBOARD_EVENT_CAPACITY);
  size_t tail = (keyboard->head + keyboard->count) % KEYBOARD_EVENT_CAPACITY;
  event.flags = keyboard->focused ? KEYBOARD_EVENT_FOCUSED : 0;
  keyboard->events[tail] = event;
  ++keyboard->count;
  struct task_wait *reader = keyboard->reader;
  keyboard->reader = NULL;
  if (reader) {
    task_wait_wake(reader);
  }
}

static void reset_keys(struct keyboard_object *keyboard)
{
  keyboard->head = keyboard->count = 0;
  memset(keyboard->down, 0, sizeof(keyboard->down));
}

void keyboard_focus(struct keyboard_object *keyboard, bool focused)
{
  KASSERT(arch_cpu_index() == 0);
  lock_keyboard(keyboard);
  if (keyboard->focused != focused) {
    keyboard->focused = focused;
    reset_keys(keyboard);
    if (keyboard->owner) {
      queue_event(keyboard, (struct keyboard_event){
        .action = focused ? KEY_FOCUS_GAINED : KEY_FOCUS_LOST,
      });
    }
  }
  unlock_keyboard(keyboard);
}

void keyboard_reset_input(struct keyboard_object *keyboard)
{
  KASSERT(arch_cpu_index() == 0);
  lock_keyboard(keyboard);
  reset_keys(keyboard);
  if (keyboard->owner) {
    queue_event(keyboard, (struct keyboard_event){.action = KEY_STATE_RESET});
  } else {
    console_input_lost(keyboard->space->console);
  }
  unlock_keyboard(keyboard);
}

static unsigned accepted_modifiers(const struct keyboard_object *keyboard, unsigned physical)
{
  unsigned modifiers = physical & (KEY_MOD_CAPS_LOCK | KEY_MOD_NUM_LOCK | KEY_MOD_SCROLL_LOCK);
  if (keyboard->down[KEY_LEFT_SHIFT] || keyboard->down[KEY_RIGHT_SHIFT]) {
    modifiers |= KEY_MOD_SHIFT;
  }
  if (keyboard->down[KEY_LEFT_CONTROL] || keyboard->down[KEY_RIGHT_CONTROL]) {
    modifiers |= KEY_MOD_CONTROL;
  }
  if (keyboard->down[KEY_LEFT_ALT] || keyboard->down[KEY_RIGHT_ALT]) {
    modifiers |= KEY_MOD_ALT;
  }
  if (keyboard->down[KEY_LEFT_SUPER] || keyboard->down[KEY_RIGHT_SUPER]) {
    modifiers |= KEY_MOD_SUPER;
  }
  return modifiers;
}

void keyboard_route_event(struct keyboard_object *keyboard, const struct key_event *event)
{
  KASSERT(arch_cpu_index() == 0 && event->key > KEY_NONE && event->key < KEY_COUNT);
  KASSERT(event->action <= KEY_REPEAT);
  lock_keyboard(keyboard);
  if (!keyboard->focused ||
      (event->action != KEY_PRESS && !keyboard->down[event->key])) {
    unlock_keyboard(keyboard);
    return;
  }
  keyboard->down[event->key] = event->action != KEY_RELEASE;
  unsigned modifiers = accepted_modifiers(keyboard, event->modifiers);
  if (keyboard->owner) {
    if (keyboard->count == KEYBOARD_EVENT_CAPACITY) {
      /* Losing a release invalidates every held key. Drop this event too and
       * require fresh presses after the reset instead of forwarding repeats. */
      reset_keys(keyboard);
      queue_event(keyboard, (struct keyboard_event){.action = KEY_STATE_RESET});
    } else {
      queue_event(keyboard, (struct keyboard_event){
        .key = event->key, .action = event->action, .modifiers = modifiers,
      });
    }
  } else {
    char bytes[KEY_TEXT_MAX];
    size_t size = keyboard_text(event, bytes);
    if (size) {
      /* Keep routing and text publication atomic against an AP acquisition. */
      console_input(keyboard->space->console, bytes, size);
    }
  }
  unlock_keyboard(keyboard);
}

static void release_keyboard(struct keyboard_object *keyboard)
{
  /* One task per process: the owner cannot release/exit while its READ sleeps. */
  KASSERT(!keyboard->reader);
  keyboard->owner = NULL;
  reset_keys(keyboard);
  console_discard_input(keyboard->space->console);
}

void keyboard_process_exit(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  struct keyboard_object *keyboard = process->space->keyboard;
  lock_keyboard(keyboard);
  if (keyboard->owner == process) {
    release_keyboard(keyboard);
  }
  unlock_keyboard(keyboard);
}

struct syscall_result keyboard_call(struct keyboard_object *keyboard, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != KEYBOARD_ACQUIRE && operation != KEYBOARD_READ && operation != KEYBOARD_RELEASE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  struct process *process = process_current();
  if (!(rights & KEYBOARD_RIGHT_INPUT) || process->space != keyboard->space) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  uint64_t flags = 0;
  struct keyboard_event reply;
  if (operation == KEYBOARD_READ) {
    if (request_size != sizeof(flags) || reply_capacity < sizeof(reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&flags, request_address, sizeof(flags)) ||
        !user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (flags & ~KEYBOARD_READ_POLL) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  } else if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  lock_keyboard(keyboard);
  if (operation == KEYBOARD_ACQUIRE) {
    enum call_status status = CALL_OK;
    if (!keyboard_available()) {
      status = CALL_UNAVAILABLE;
    } else if (keyboard->owner) {
      status = CALL_BUSY;
    } else {
      keyboard->owner = process;
      reset_keys(keyboard);
      console_discard_input(keyboard->space->console);
      queue_event(keyboard, (struct keyboard_event){
        .action = keyboard->focused ? KEY_FOCUS_GAINED : KEY_FOCUS_LOST,
      });
    }
    unlock_keyboard(keyboard);
    return (struct syscall_result){status, 0};
  }
  if (keyboard->owner != process) {
    unlock_keyboard(keyboard);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == KEYBOARD_RELEASE) {
    release_keyboard(keyboard);
    unlock_keyboard(keyboard);
    return (struct syscall_result){CALL_OK, 0};
  }

  while (!keyboard->count) {
    if (task_stop_requested()) {
      unlock_keyboard(keyboard);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
    if (flags & KEYBOARD_READ_POLL) {
      unlock_keyboard(keyboard);
      return (struct syscall_result){CALL_TIMED_OUT, 0};
    }
    struct task_wait *wait = task_wait_prepare();
    KASSERT(!keyboard->reader);
    keyboard->reader = wait;
    unlock_keyboard(keyboard);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_keyboard(keyboard);
    if (keyboard->reader == wait) {
      keyboard->reader = NULL;
    }
    KASSERT(!keyboard->reader && keyboard->owner == process);
    if (!resumed || task_stop_requested()) {
      unlock_keyboard(keyboard);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
  }
  reply = keyboard->events[keyboard->head];
  keyboard->head = (keyboard->head + 1) % KEYBOARD_EVENT_CAPACITY;
  --keyboard->count;
  unlock_keyboard(keyboard);

  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
