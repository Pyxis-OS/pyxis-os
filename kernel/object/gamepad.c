#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/gamepad.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/gamepad.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user_memory.h>

/* Adapter and space registries are BSP-owned and retain their backing until
 * reboot. AP callers observe only the per-space copies under that object's lock. */
static struct gamepad_source *sources[GAMEPAD_DEVICE_CAPACITY];
static struct gamepad_object *objects;
static uint64_t next_instance, catalog_generation;

static void assert_source_owner(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
}

static void lock_gamepad(struct gamepad_object *gamepad)
{
  while (atomic_exchange_explicit(&gamepad->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_gamepad(struct gamepad_object *gamepad)
{
  atomic_store_explicit(&gamepad->locked, false, memory_order_release);
}

static bool capture_focused(const struct gamepad_object *gamepad)
{
  return gamepad->selected && !gamepad->terminal_layer && !gamepad->overlay_focused;
}

static void rearm_device(struct gamepad_device *device)
{
  device->accepted = (struct gamepad_state){0};
  device->accepted_valid = false;
  device->suppressed_buttons = device->physical_valid ? device->physical.buttons : UINT32_MAX;
  device->hat_suppressed = !device->physical_valid || device->physical.hat != GAMEPAD_HAT_NEUTRAL;
}

static void reset_state(struct gamepad_object *gamepad)
{
  gamepad->head = gamepad->count = 0;
  for (size_t i = 0; i < GAMEPAD_DEVICE_CAPACITY; ++i) {
    rearm_device(&gamepad->devices[i]);
  }
}

static void wake_reader(struct gamepad_object *gamepad)
{
  struct task_wait *reader = gamepad->reader;
  gamepad->reader = NULL;
  if (reader) {
    task_wait_wake(reader);
  }
}

/* No user copying or allocation while locked. Notification follows unlock. */
static void queue_event(struct gamepad_object *gamepad, struct gamepad_event event)
{
  if (!gamepad->owner || gamepad->exhausted) {
    return;
  }
  event.flags |= capture_focused(gamepad) ? GAMEPAD_EVENT_FOCUSED : 0;
  if (gamepad->sequence == UINT64_MAX) {
    gamepad->exhausted = true;
    reset_state(gamepad);
    wake_reader(gamepad);
    return;
  }
  event.sequence = ++gamepad->sequence;
  event.catalog_generation = gamepad->catalog_generation;
  if (gamepad->count == GAMEPAD_EVENT_CAPACITY) {
    size_t tail = (gamepad->head + gamepad->count - 1) % GAMEPAD_EVENT_CAPACITY;
    struct gamepad_event *last = &gamepad->events[tail];
    if (event.type == GAMEPAD_INPUT && last->type == GAMEPAD_INPUT &&
        event.instance == last->instance && event.flags == last->flags &&
        event.state.buttons == last->state.buttons && event.state.hat == last->state.hat) {
      *last = event;
      return;
    }
    reset_state(gamepad);
    event = (struct gamepad_event){
      .type = GAMEPAD_RESET, .sequence = gamepad->sequence,
      .catalog_generation = gamepad->catalog_generation,
      .flags = capture_focused(gamepad) ? GAMEPAD_EVENT_FOCUSED : 0,
    };
  }
  size_t tail = (gamepad->head + gamepad->count) % GAMEPAD_EVENT_CAPACITY;
  gamepad->events[tail] = event;
  ++gamepad->count;
  wake_reader(gamepad);
}

static void copy_source(struct gamepad_device *device, const struct gamepad_source *source)
{
  *device = (struct gamepad_device){
    .info = source->info, .physical = source->physical,
    .live = true, .physical_valid = source->valid,
  };
  rearm_device(device);
}

bool gamepad_source_attach(struct gamepad_source *source,
    uint16_t vendor, uint16_t product, uint16_t version)
{
  assert_source_owner();
  KASSERT(source);
  if (source->registered || next_instance == UINT64_MAX ||
      catalog_generation > UINT64_MAX - GAMEPAD_DEVICE_CAPACITY - 1) {
    return false;
  }
  size_t slot = 0;
  while (slot < GAMEPAD_DEVICE_CAPACITY && sources[slot]) {
    ++slot;
  }
  if (slot == GAMEPAD_DEVICE_CAPACITY) {
    return false;
  }
  *source = (struct gamepad_source){
    .registered = true, .live = true,
    .info = {.instance = ++next_instance, .vendor = vendor, .product = product,
      .version = version, .profile = GAMEPAD_PROFILE_DUALSENSE_EDGE_USB,
      .name = "DualSense Edge USB"},
  };
  sources[slot] = source;
  ++catalog_generation;
  for (struct gamepad_object *gamepad = objects; gamepad; gamepad = gamepad->next) {
    lock_gamepad(gamepad);
    copy_source(&gamepad->devices[slot], source);
    gamepad->catalog_generation = catalog_generation;
    queue_event(gamepad, (struct gamepad_event){
      .type = GAMEPAD_ATTACHED, .instance = source->info.instance,
    });
    unlock_gamepad(gamepad);
  }
  readiness_notify();
  return true;
}

static size_t source_slot(const struct gamepad_source *source)
{
  for (size_t i = 0; i < GAMEPAD_DEVICE_CAPACITY; ++i) {
    if (sources[i] == source) {
      return i;
    }
  }
  return GAMEPAD_DEVICE_CAPACITY;
}

void gamepad_source_report(struct gamepad_source *source, const struct gamepad_state *state)
{
  assert_source_owner();
  if (!source->live) {
    return;
  }
  size_t slot = source_slot(source);
  KASSERT(slot < GAMEPAD_DEVICE_CAPACITY);
  source->physical = *state;
  source->valid = true;
  bool published = false;
  for (struct gamepad_object *gamepad = objects; gamepad; gamepad = gamepad->next) {
    lock_gamepad(gamepad);
    struct gamepad_device *device = &gamepad->devices[slot];
    device->physical = *state;
    device->physical_valid = true;
    if (gamepad->owner && capture_focused(gamepad)) {
      device->suppressed_buttons &= state->buttons;
      if (state->hat == GAMEPAD_HAT_NEUTRAL) {
        device->hat_suppressed = false;
      }
      struct gamepad_state accepted = *state;
      accepted.buttons &= ~device->suppressed_buttons;
      if (device->hat_suppressed) {
        accepted.hat = GAMEPAD_HAT_NEUTRAL;
      }
      if (!device->accepted_valid || memcmp(&accepted, &device->accepted, sizeof(accepted))) {
        device->accepted = accepted;
        device->accepted_valid = true;
        queue_event(gamepad, (struct gamepad_event){
          .type = GAMEPAD_INPUT, .instance = source->info.instance,
          .flags = GAMEPAD_EVENT_VALID, .state = accepted,
        });
        published = true;
      }
    }
    unlock_gamepad(gamepad);
  }
  if (published) {
    readiness_notify();
  }
}

void gamepad_source_lost(struct gamepad_source *source)
{
  assert_source_owner();
  if (!source->live) {
    return;
  }
  size_t slot = source_slot(source);
  KASSERT(slot < GAMEPAD_DEVICE_CAPACITY);
  sources[slot] = NULL;
  source->live = source->valid = false;
  /* Admission reserves enough generations for every live source to retire. */
  KASSERT(catalog_generation != UINT64_MAX);
  ++catalog_generation;
  for (struct gamepad_object *gamepad = objects; gamepad; gamepad = gamepad->next) {
    lock_gamepad(gamepad);
    gamepad->devices[slot].live = false;
    gamepad->devices[slot].physical_valid = false;
    rearm_device(&gamepad->devices[slot]);
    gamepad->catalog_generation = catalog_generation;
    queue_event(gamepad, (struct gamepad_event){
      .type = GAMEPAD_REMOVED, .instance = source->info.instance,
    });
    unlock_gamepad(gamepad);
  }
  readiness_notify();
}

static void destroy_gamepad(struct kernel_object *object)
{
  struct gamepad_object *gamepad = (struct gamepad_object *)object;
  KASSERT(!gamepad->owner && !gamepad->reader && !gamepad->read_active);
  /* Spaces retain the registry's initial reference until reboot. */
  struct gamepad_object **link = &objects;
  while (*link && *link != gamepad) {
    link = &(*link)->next;
  }
  if (*link) {
    *link = gamepad->next;
  }
  kfree(gamepad);
}

struct gamepad_object *gamepad_create(struct space *space, bool selected)
{
  assert_source_owner();
  struct gamepad_object *gamepad = kmalloc(sizeof(*gamepad));
  if (!gamepad) {
    return NULL;
  }
  *gamepad = (struct gamepad_object){
    .space = space, .selected = selected, .catalog_generation = catalog_generation,
    .next = objects,
  };
  atomic_init(&gamepad->locked, false);
  object_init(&gamepad->object, OBJECT_GAMEPAD, destroy_gamepad);
  for (size_t i = 0; i < GAMEPAD_DEVICE_CAPACITY; ++i) {
    if (sources[i]) {
      copy_source(&gamepad->devices[i], sources[i]);
    }
  }
  objects = gamepad;
  return gamepad;
}

static void publish_focus(struct gamepad_object *gamepad, bool previous)
{
  if (previous == capture_focused(gamepad)) {
    return;
  }
  reset_state(gamepad);
  queue_event(gamepad, (struct gamepad_event){
    .type = capture_focused(gamepad) ? GAMEPAD_FOCUS_GAINED : GAMEPAD_FOCUS_LOST,
  });
}

void gamepad_focus(struct gamepad_object *gamepad, bool selected)
{
  assert_source_owner();
  lock_gamepad(gamepad);
  bool previous = capture_focused(gamepad);
  gamepad->selected = selected;
  publish_focus(gamepad, previous);
  unlock_gamepad(gamepad);
  readiness_notify();
}

void gamepad_set_layer(struct gamepad_object *gamepad, bool terminal_layer)
{
  assert_source_owner();
  lock_gamepad(gamepad);
  bool previous = capture_focused(gamepad);
  gamepad->terminal_layer = terminal_layer;
  publish_focus(gamepad, previous);
  unlock_gamepad(gamepad);
  readiness_notify();
}

void gamepad_set_overlay(struct gamepad_object *gamepad, bool focused)
{
  assert_source_owner();
  lock_gamepad(gamepad);
  bool previous = capture_focused(gamepad);
  gamepad->overlay_focused = focused;
  publish_focus(gamepad, previous);
  unlock_gamepad(gamepad);
  readiness_notify();
}

bool gamepad_owned(struct gamepad_object *gamepad, struct process *process)
{
  uint64_t flags = cpu_save_interrupts();
  lock_gamepad(gamepad);
  bool owned = gamepad->owner == process;
  unlock_gamepad(gamepad);
  cpu_restore_interrupts(flags);
  return owned;
}

uint64_t gamepad_ready(struct gamepad_object *gamepad, struct process *process)
{
  uint64_t flags = cpu_save_interrupts();
  lock_gamepad(gamepad);
  uint64_t ready = gamepad->owner != process || gamepad->exhausted ? WAIT_ERROR :
      (gamepad->count ? WAIT_READABLE : 0);
  unlock_gamepad(gamepad);
  cpu_restore_interrupts(flags);
  return ready;
}

static void release_gamepad(struct gamepad_object *gamepad)
{
  gamepad->owner = NULL;
  reset_state(gamepad);
  wake_reader(gamepad);
}

void gamepad_process_exit(struct process *process)
{
  assert_source_owner();
  struct gamepad_object *gamepad = process->space->gamepad;
  lock_gamepad(gamepad);
  bool owned = gamepad->owner == process;
  if (owned) {
    release_gamepad(gamepad);
  }
  unlock_gamepad(gamepad);
  if (owned) {
    readiness_notify();
  }
}

static struct syscall_result read_gamepad(struct gamepad_object *gamepad,
    struct process *process, uint64_t flags, uintptr_t address)
{
  if (gamepad->read_active) {
    unlock_gamepad(gamepad);
    return (struct syscall_result){CALL_BUSY, 0};
  }
  gamepad->read_active = true;
  uint64_t acquisition = gamepad->acquisition;
  while (!gamepad->count) {
    if (task_stop_requested() || task_operation_denied() || gamepad->exhausted) {
      gamepad->read_active = false;
      unlock_gamepad(gamepad);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
    if (flags & GAMEPAD_READ_POLL) {
      gamepad->read_active = false;
      unlock_gamepad(gamepad);
      return (struct syscall_result){CALL_TIMED_OUT, 0};
    }
    struct task_wait *wait = task_wait_prepare();
    gamepad->reader = wait;
    unlock_gamepad(gamepad);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_gamepad(gamepad);
    if (gamepad->reader == wait) {
      gamepad->reader = NULL;
    }
    if (!resumed || task_stop_requested() || task_operation_denied() ||
        gamepad->owner != process || gamepad->acquisition != acquisition) {
      gamepad->read_active = false;
      unlock_gamepad(gamepad);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
  }
  if (!task_operation_start()) {
    gamepad->read_active = false;
    unlock_gamepad(gamepad);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct gamepad_event event = gamepad->events[gamepad->head];
  gamepad->head = (gamepad->head + 1) % GAMEPAD_EVENT_CAPACITY;
  --gamepad->count;
  unlock_gamepad(gamepad);
  bool copied = copy_to_user(address, &event, sizeof(event));
  lock_gamepad(gamepad);
  gamepad->read_active = false;
  if (!copied && gamepad->owner == process && gamepad->acquisition == acquisition) {
    reset_state(gamepad);
    queue_event(gamepad, (struct gamepad_event){.type = GAMEPAD_RESET});
  }
  unlock_gamepad(gamepad);
  if (!copied) {
    readiness_notify();
  }
  return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER,
    copied ? sizeof(event) : 0};
}

struct syscall_result gamepad_call(struct gamepad_object *gamepad, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation < GAMEPAD_ACQUIRE || operation > GAMEPAD_STATE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  struct process *process = process_current();
  if (!(rights & GAMEPAD_RIGHT_INPUT) || process->space != gamepad->space) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  size_t payload = 0, reply_size = 0;
  if (operation == GAMEPAD_READ) {
    payload = sizeof(uint64_t);
    reply_size = sizeof(struct gamepad_event);
  } else if (operation == GAMEPAD_CATALOG) {
    reply_size = sizeof(struct gamepad_catalog);
  } else if (operation == GAMEPAD_INFO || operation == GAMEPAD_STATE) {
    payload = sizeof(uint64_t);
    reply_size = operation == GAMEPAD_INFO ? sizeof(struct gamepad_info) : sizeof(struct gamepad_event);
  }
  if (request_size != payload || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t value = 0;
  if ((payload && !copy_from_user(&value, request_address, payload)) ||
      (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == GAMEPAD_READ && (value & ~GAMEPAD_READ_POLL)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (operation != GAMEPAD_READ && !task_operation_start()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  lock_gamepad(gamepad);
  if (gamepad->exhausted) {
    unlock_gamepad(gamepad);
    return (struct syscall_result){CALL_LIMIT, 0};
  }
  if (operation == GAMEPAD_ACQUIRE) {
    enum call_status status = CALL_OK;
    if (gamepad->owner || gamepad->read_active) {
      status = CALL_BUSY;
    } else if (gamepad->acquisition == UINT64_MAX) {
      status = CALL_LIMIT;
    } else {
      ++gamepad->acquisition;
      gamepad->owner = process;
      reset_state(gamepad);
      queue_event(gamepad, (struct gamepad_event){.type = GAMEPAD_RESET});
    }
    unlock_gamepad(gamepad);
    if (status == CALL_OK) {
      readiness_notify();
    }
    return (struct syscall_result){status, 0};
  }
  if (gamepad->owner != process) {
    unlock_gamepad(gamepad);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == GAMEPAD_RELEASE) {
    if (gamepad->read_active) {
      unlock_gamepad(gamepad);
      return (struct syscall_result){CALL_BUSY, 0};
    }
    release_gamepad(gamepad);
    unlock_gamepad(gamepad);
    readiness_notify();
    return (struct syscall_result){CALL_OK, 0};
  }
  if (operation == GAMEPAD_READ) {
    return read_gamepad(gamepad, process, value, reply_address);
  }
  union {
    struct gamepad_catalog catalog;
    struct gamepad_info info;
    struct gamepad_event event;
  } reply = {0};
  if (operation == GAMEPAD_CATALOG) {
    reply.catalog.generation = gamepad->catalog_generation;
    for (size_t i = 0; i < GAMEPAD_DEVICE_CAPACITY; ++i) {
      if (gamepad->devices[i].live) {
        reply.catalog.instances[reply.catalog.count++] = gamepad->devices[i].info.instance;
      }
    }
  } else {
    struct gamepad_device *device = NULL;
    for (size_t i = 0; i < GAMEPAD_DEVICE_CAPACITY; ++i) {
      if (gamepad->devices[i].live && gamepad->devices[i].info.instance == value) {
        device = &gamepad->devices[i];
        break;
      }
    }
    if (!device) {
      unlock_gamepad(gamepad);
      return (struct syscall_result){CALL_NOT_FOUND, 0};
    }
    if (operation == GAMEPAD_INFO) {
      reply.info = device->info;
    } else {
      reply.event = (struct gamepad_event){
        .instance = value, .sequence = gamepad->sequence,
        .catalog_generation = gamepad->catalog_generation, .type = GAMEPAD_INPUT,
        .flags = capture_focused(gamepad) ? GAMEPAD_EVENT_FOCUSED : 0,
      };
      if (device->accepted_valid && capture_focused(gamepad)) {
        reply.event.flags |= GAMEPAD_EVENT_VALID;
        reply.event.state = device->accepted;
      }
    }
  }
  unlock_gamepad(gamepad);
  bool copied = copy_to_user(reply_address, &reply, reply_size);
  return (struct syscall_result){copied ? CALL_OK : CALL_BAD_BUFFER,
    copied ? reply_size : 0};
}
