#include <abi/clipboard.h>
#include <abi/terminal.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/object/clipboard.h>
#include <kernel/object/console.h>
#include <kernel/object/terminal.h>
#include <kernel/object/pointer.h>
#include <kernel/object/display.h>
#include <kernel/object/process.h>
#include <kernel/object/capability.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/pointer.h>
#include <kernel/keyboard.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user_memory.h>

#define CLIPBOARD_ALLOCATE UINT64_C(100)
#define CLIPBOARD_COMMIT UINT64_C(101)
#define CLIPBOARD_CONTROLLER_REFUSE UINT64_C(102)
#define CLIPBOARD_READ_CHUNK 256

struct clipboard_item {
  struct kernel_object object;
  size_t length, allocated;
  uint8_t bytes[];
};

struct clipboard_store {
  struct kernel_object object;
  struct space *space; /* NULL for the independently authorized shared layer. */
  struct clipboard_item *item;
};

struct clipboard_activation {
  struct process *owner;
  uint64_t id, operation, layer, generation, mapping_identity, deadline, receiver_epoch;
  enum call_status refusal;
};

struct clipboard_space {
  struct clipboard_store local;
  struct clipboard_activation activation;
  struct clipboard_receiver *transaction;
  struct clipboard_space *next;
  struct space *space;
  bool enter_blocked[2], resume_pending;
};

struct clipboard_receiver {
  struct clipboard_receiver *next;
  struct process *owner;
  struct kernel_object *input, *output;
  struct clipboard_input target;
  struct clipboard_item *item;
  struct task_wait *wait;
  uint64_t epoch, transaction_id, deadline, phase, status;
  size_t offset;
  bool boundary, terminator_delivered, interrupt_pending, invalid;
};

static struct clipboard_store shared;
static bool shared_initialized;
static struct clipboard_space *spaces;
static struct clipboard_receiver *receivers;
static atomic_bool locked;
static size_t retained_bytes;
static uint64_t next_action, next_epoch, next_transaction;
static struct task_wait *worker_wait;
static bool worker_notified, worker_started;
static bool command_held[KEY_COUNT];
static bool enter_down[2];

static void lock_clipboard(void)
{
  while (atomic_exchange_explicit(&locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_clipboard(void)
{
  atomic_store_explicit(&locked, false, memory_order_release);
}

static void lock_input(struct clipboard_input *input)
{
  while (atomic_exchange_explicit(input->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_input(struct clipboard_input *input)
{
  atomic_store_explicit(input->locked, false, memory_order_release);
}

static void wake_receiver(struct clipboard_receiver *receiver)
{
  struct task_wait *wait = receiver->wait;
  receiver->wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void notify_worker_locked(void)
{
  worker_notified = true;
  struct task_wait *wait = worker_wait;
  worker_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

void clipboard_stop_notify(void)
{
  if (!worker_started) {
    return;
  }
  lock_clipboard();
  notify_worker_locked();
  unlock_clipboard();
}

static void resume_input_locked(struct clipboard_space *state, bool input_complete)
{
  state->enter_blocked[0] |= enter_down[0];
  state->enter_blocked[1] |= enter_down[1];
  state->resume_pending = !input_complete;
  if (!input_complete) {
    notify_worker_locked();
  }
}

static void destroy_item(struct kernel_object *object)
{
  struct clipboard_item *item = (struct clipboard_item *)object;
  lock_clipboard();
  KASSERT(retained_bytes >= item->allocated);
  retained_bytes -= item->allocated;
  unlock_clipboard();
  kfree(item);
}

static struct clipboard_item *allocate_item(size_t length)
{
  KASSERT(arch_cpu_index() == 0);
  if (length > CLIPBOARD_TEXT_MAX) {
    return NULL;
  }
  lock_clipboard();
  bool fits = length <= CLIPBOARD_STORAGE_MAX - retained_bytes;
  if (fits) {
    retained_bytes += length;
  }
  unlock_clipboard();
  if (!fits) {
    return NULL;
  }
  struct clipboard_item *item = kmalloc(sizeof(*item) + length);
  if (!item) {
    lock_clipboard();
    retained_bytes -= length;
    unlock_clipboard();
    return NULL;
  }
  object_init(&item->object, OBJECT_CLIPBOARD, destroy_item);
  item->length = item->allocated = length;
  return item;
}

static void destroy_store(struct kernel_object *object)
{
  (void)object;
  /* The space registry and boot session retain their initial store references. */
  panic("clipboard store lost its lifetime owner");
}

bool clipboard_space_init(struct space *space)
{
  struct clipboard_space *state = kmalloc(sizeof(*state));
  if (!state) {
    return false;
  }
  *state = (struct clipboard_space){.space = space};
  object_init(&state->local.object, OBJECT_CLIPBOARD, destroy_store);
  state->local.space = space;
  space->clipboard = state;
  if (!shared_initialized) {
    object_init(&shared.object, OBJECT_CLIPBOARD, destroy_store);
    shared_initialized = true;
  }
  lock_clipboard();
  state->next = spaces;
  spaces = state;
  unlock_clipboard();
  return true;
}

struct kernel_object *clipboard_local(struct space *space)
{
  return &space->clipboard->local.object;
}

struct kernel_object *clipboard_shared(void)
{
  return &shared.object;
}

bool clipboard_input_descriptor(struct kernel_object *object, struct clipboard_input *input)
{
  if (object->type != OBJECT_CONSOLE) {
    return terminal_clipboard_input(object, input);
  }
  struct console_object *console = (struct console_object *)object;
  *input = (struct clipboard_input){
    .object = object, .output = object, .space = console->space,
    .locked = &console->input_locked, .bytes = (uint8_t *)console->input,
    .capacity = CONSOLE_INPUT_CAPACITY, .head = &console->input_head,
    .count = &console->input_count, .lost = &console->input_lost,
    .reader_active = &console->reader_active, .reader_process = &console->reader_process,
    .first_reader = &console->first_reader, .input_wait = &console->input_wait,
    .interrupt = &console->interrupt,
  };
  return true;
}

static struct clipboard_receiver *find_receiver(struct kernel_object *input)
{
  for (struct clipboard_receiver *receiver = receivers; receiver; receiver = receiver->next) {
    if (receiver->input == input && !receiver->invalid) {
      return receiver;
    }
  }
  return NULL;
}

static bool active(struct clipboard_receiver *receiver)
{
  return receiver && receiver->transaction_id;
}

bool clipboard_ordinary_allowed(struct kernel_object *object)
{
  lock_clipboard();
  bool allowed = !active(find_receiver(object));
  unlock_clipboard();
  return allowed;
}

void clipboard_ordinary_transfer(struct kernel_object *object)
{
  lock_clipboard();
  struct clipboard_receiver *receiver = find_receiver(object);
  if (receiver) {
    receiver->boundary = false;
  }
  unlock_clipboard();
}

void clipboard_input_notify(struct kernel_object *object)
{
  lock_clipboard();
  struct clipboard_receiver *receiver = find_receiver(object);
  if (receiver) {
    wake_receiver(receiver);
  }
  unlock_clipboard();
}

uint64_t clipboard_input_ready(struct kernel_object *object, struct process *caller)
{
  uint64_t flags = cpu_save_interrupts();
  lock_clipboard();
  struct clipboard_receiver *receiver = find_receiver(object);
  uint64_t ready = 0;
  if (active(receiver)) {
    if (receiver->owner == caller && !receiver->terminator_delivered) {
      ready = WAIT_READABLE;
    }
  }
  unlock_clipboard();
  cpu_restore_interrupts(flags);
  return ready;
}

static void cancel_locked(struct clipboard_receiver *receiver, enum call_status reason)
{
  if (!active(receiver) || receiver->phase == CONSOLE_PASTE_CANCEL) {
    return;
  }
  bool terminated = receiver->phase == CONSOLE_PASTE_END && receiver->terminator_delivered;
  receiver->phase = CONSOLE_PASTE_CANCEL;
  receiver->status = reason;
  receiver->terminator_delivered = terminated;
  if (receiver->item) {
    object_release(&receiver->item->object);
    receiver->item = NULL;
  }
  wake_receiver(receiver);
}

void clipboard_space_cancel(struct space *space)
{
  if (!space || !space->clipboard) {
    return;
  }
  lock_clipboard();
  space->clipboard->activation = (struct clipboard_activation){0};
  cancel_locked(space->clipboard->transaction, CALL_ABANDONED);
  unlock_clipboard();
  readiness_notify();
}

void clipboard_input_hangup(struct kernel_object *object)
{
  lock_clipboard();
  struct clipboard_receiver *receiver = find_receiver(object);
  if (receiver) {
    receiver->invalid = true;
    receiver->boundary = false;
    cancel_locked(receiver, CALL_ENDPOINT_CLOSED);
    struct clipboard_space *state = receiver->owner->space->clipboard;
    if (state->transaction == receiver) {
      state->transaction = NULL;
      resume_input_locked(state, false);
    }
    receiver->transaction_id = 0;
    wake_receiver(receiver);
  }
  unlock_clipboard();
}

static void release_receiver(struct process *process, bool input_complete)
{
  struct clipboard_receiver *receiver = process->paste_receiver;
  if (!receiver) {
    return;
  }
  lock_clipboard();
  KASSERT(!receiver->wait);
  struct clipboard_receiver **link = &receivers;
  while (*link != receiver) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = receiver->next;
  struct clipboard_space *state = process->space->clipboard;
  if (state->transaction == receiver) {
    state->transaction = NULL;
    resume_input_locked(state, input_complete);
  }
  process->paste_receiver = NULL;
  struct clipboard_item *item = receiver->item;
  unlock_clipboard();
  if (item) {
    object_release(&item->object);
  }
  object_release(receiver->input);
  object_release(receiver->output);
  kfree(receiver);
  readiness_notify();
}

void clipboard_process_exit(struct process *process)
{
  if (process->paste_receiver) {
    bool input_complete = space_keyboard_sync_input();
    release_receiver(process, input_complete);
  }
  lock_clipboard();
  struct clipboard_space *state = process->space->clipboard;
  if (state->activation.owner == process) {
    state->activation = (struct clipboard_activation){0};
  }
  if (process->space->terminal_pointer->owner == process) {
    cancel_locked(state->transaction, CALL_ENDPOINT_CLOSED);
  }
  unlock_clipboard();
}

static bool input_clean(const struct clipboard_input *input, struct process *owner)
{
  return !*input->count && (!input->lost || !*input->lost) &&
      (!input->closed || !*input->closed) && (!input->hung_up || !*input->hung_up) &&
      (!*input->reader_active || *input->reader_process == owner) && !*input->first_reader;
}

static enum call_status admit_paste(struct space *space, struct clipboard_store *store,
    struct kernel_object *input_object, uint64_t expected_epoch, uint64_t *transaction_id)
{
  struct clipboard_input target, outer;
  if (!clipboard_input_descriptor(input_object, &target) || target.space != space) {
    return CALL_DENIED;
  }
  clipboard_input_descriptor(&space->console->object, &outer);
  lock_input(&outer);
  bool distinct = target.locked != outer.locked;
  if (distinct) {
    lock_input(&target);
  }
  lock_clipboard();
  struct clipboard_receiver *receiver = find_receiver(input_object);
  enum call_status status = CALL_OK;
  if (!receiver) {
    status = CALL_UNAVAILABLE;
  } else if ((expected_epoch && receiver->epoch != expected_epoch) ||
      space->clipboard->transaction || space->clipboard->resume_pending || (distinct && !input_clean(&outer,
        space->terminal_pointer->owner)) || !receiver->boundary ||
      !input_clean(&target, receiver->owner)) {
    status = CALL_BUSY;
  } else if (process_control_stopped(receiver->owner->control)) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (!store->item) {
    status = CALL_NOT_FOUND;
  } else if (next_transaction == UINT64_MAX || !object_retain(&store->item->object)) {
    status = CALL_LIMIT;
  } else {
    receiver->item = store->item;
    receiver->offset = 0;
    receiver->transaction_id = ++next_transaction;
    receiver->phase = CONSOLE_PASTE_BEGIN;
    receiver->deadline = task_deadline_after_ms(CLIPBOARD_TIMEOUT_MS);
    receiver->status = CALL_OK;
    receiver->terminator_delivered = false;
    receiver->interrupt_pending = false;
    space->clipboard->transaction = receiver;
    *transaction_id = receiver->transaction_id;
    wake_receiver(receiver);
    notify_worker_locked();
  }
  unlock_clipboard();
  if (distinct) {
    unlock_input(&target);
  }
  unlock_input(&outer);
  if (status == CALL_OK) {
    readiness_notify();
  }
  return status;
}

static enum call_status consume_activation(struct clipboard_request *request)
{
  struct space *space = request->process->space;
  struct clipboard_store *store = (struct clipboard_store *)request->object;
  if (store->space && store->space != space) {
    return CALL_DENIED;
  }
  uint64_t operation = (request->operation == CLIPBOARD_ALLOCATE || request->operation == CLIPBOARD_CLEAR) ?
      CLIPBOARD_PUBLISH : request->operation == CLIPBOARD_REFUSE ? request->length : request->operation;
  uint64_t layer = store->space ? CLIPBOARD_LAYER_LOCAL : CLIPBOARD_LAYER_SHARED;
  struct pointer_geometry geometry = pointer_surface_geometry(space->terminal_pointer);
  if (space_pointer_active() != space || space->display->visible ||
      space->terminal_pointer->owner != request->process ||
      geometry.generation != request->generation || geometry.mapping_identity != request->mapping_identity ||
      process_control_stopped(request->process->control)) {
    return CALL_DENIED;
  }
  lock_clipboard();
  struct clipboard_activation activation = space->clipboard->activation;
  bool matches = activation.owner == request->process && activation.id &&
      activation.id == request->action_id && activation.operation == operation &&
      activation.layer == layer && activation.generation == request->generation &&
      activation.mapping_identity == request->mapping_identity && arch_monotonic_ns() < activation.deadline;
  if (matches) {
    space->clipboard->activation = (struct clipboard_activation){0};
  }
  unlock_clipboard();
  return matches ? CALL_OK : CALL_DENIED;
}

void clipboard_request_execute(struct clipboard_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  request->status = CALL_OK;
  if (request->operation == CLIPBOARD_CONTROLLER_REFUSE) {
    space_keyboard_sync_input();
    struct space *space = request->process->space;
    struct pointer_geometry geometry = pointer_surface_geometry(space->terminal_pointer);
    lock_clipboard();
    struct clipboard_activation action = space->clipboard->activation;
    bool matches = action.owner == request->process && action.id && action.id == request->action_id &&
        action.operation == request->length && action.generation == request->generation &&
        action.mapping_identity == request->mapping_identity && geometry.generation == request->generation &&
        geometry.mapping_identity == request->mapping_identity &&
        space->terminal_pointer->owner == request->process;
    if (matches) {
      space->clipboard->activation = (struct clipboard_activation){0};
    }
    unlock_clipboard();
    request->status = matches ? CALL_OK : CALL_DENIED;
    return;
  }
  if (request->operation == CONSOLE_PASTE_REGISTER) {
    struct clipboard_input input;
    if (!clipboard_input_descriptor(request->object, &input) ||
        input.space != request->process->space || input.output != request->output) {
      request->status = CALL_DENIED;
      return;
    }
    lock_input(&input);
    bool hung_up = input.hung_up && *input.hung_up;
    unlock_input(&input);
    if (hung_up) {
      request->status = CALL_ENDPOINT_CLOSED;
      return;
    }
    lock_clipboard();
    bool busy = request->process->paste_receiver || find_receiver(request->object);
    unlock_clipboard();
    if (busy) {
      request->status = CALL_BUSY;
      return;
    }
    if (next_epoch == UINT64_MAX) {
      request->status = CALL_LIMIT;
      return;
    }
    struct clipboard_receiver *receiver = kmalloc(sizeof(*receiver));
    if (!receiver) {
      request->status = CALL_NO_MEMORY;
      return;
    }
    if (!object_retain(request->object)) {
      kfree(receiver);
      request->status = CALL_LIMIT;
      return;
    }
    if (!object_retain(request->output)) {
      object_release(request->object);
      kfree(receiver);
      request->status = CALL_LIMIT;
      return;
    }
    *receiver = (struct clipboard_receiver){.owner = request->process,
      .input = request->object, .output = request->output, .target = input,
      .epoch = ++next_epoch};
    lock_clipboard();
    receiver->next = receivers;
    receivers = receiver;
    request->process->paste_receiver = receiver;
    unlock_clipboard();
    request->epoch = receiver->epoch;
    return;
  }
  if (request->operation == CONSOLE_PASTE_RELEASE || request->operation == CONSOLE_PASTE_ACK) {
    bool input_complete = space_keyboard_sync_input();
    struct clipboard_receiver *receiver = request->process->paste_receiver;
    if (!receiver || receiver->input != request->object || receiver->epoch != request->epoch) {
      request->status = CALL_DENIED;
      return;
    }
    if (request->operation == CONSOLE_PASTE_RELEASE) {
      release_receiver(request->process, input_complete);
      return;
    }
    lock_clipboard();
    if (!active(receiver) || receiver->transaction_id != request->transaction_id ||
        !receiver->terminator_delivered || (receiver->phase != CONSOLE_PASTE_END &&
        receiver->phase != CONSOLE_PASTE_CANCEL)) {
      request->status = CALL_BAD_REQUEST;
      unlock_clipboard();
      return;
    }
    struct clipboard_space *state = request->process->space->clipboard;
    bool interrupt = receiver->interrupt_pending;
    struct clipboard_item *item = receiver->item;
    receiver->item = NULL;
    receiver->transaction_id = 0;
    receiver->phase = 0;
    receiver->boundary = false;
    state->transaction = NULL;
    resume_input_locked(state, input_complete);
    unlock_clipboard();
    if (item) {
      object_release(&item->object);
    }
    if (interrupt) {
      struct clipboard_input *input = &receiver->target;
      lock_input(input);
      const uint8_t byte = 3;
      size_t consumed = console_interrupt_scan(input->interrupt, &byte, 1);
      if (consumed) {
        *input->head = *input->count = 0;
        if (input->lost) {
          *input->lost = false;
        }
      } else if (*input->count < input->capacity) {
        size_t tail = (*input->head + *input->count) % input->capacity;
        input->bytes[tail] = byte;
        ++*input->count;
      }
      struct task_wait *wait = *input->input_wait;
      *input->input_wait = NULL;
      if (wait) {
        task_wait_wake(wait);
      }
      unlock_input(input);
    }
    readiness_notify();
    return;
  }
  if (request->operation == CLIPBOARD_COMMIT) {
    struct clipboard_store *store = (struct clipboard_store *)request->object;
    struct pointer_geometry geometry = pointer_surface_geometry(request->process->space->terminal_pointer);
    if (request->process->space->terminal_pointer->owner != request->process ||
        request->process->space->display->visible || space_pointer_active() != request->process->space ||
        geometry.generation != request->generation || geometry.mapping_identity != request->mapping_identity ||
        process_control_stopped(request->process->control)) {
      request->status = CALL_ABANDONED;
      return;
    }
    lock_clipboard();
    struct clipboard_item *previous = store->item;
    request->item->length = request->length;
    store->item = request->item;
    request->item = NULL;
    unlock_clipboard();
    if (previous) {
      object_release(&previous->object);
    }
    return;
  }
  bool input_complete = space_keyboard_sync_input();
  request->status = consume_activation(request);
  if (request->status == CALL_OK && request->refusal != CALL_OK) {
    request->status = request->refusal;
  }
  if (request->status == CALL_OK && request->operation == CLIPBOARD_PASTE && !input_complete) {
    request->status = CALL_BUSY;
  }
  if (request->status != CALL_OK) {
    return;
  }
  if (request->operation == CLIPBOARD_REFUSE) {
    return;
  }
  if (request->operation == CLIPBOARD_ALLOCATE) {
    request->item = allocate_item(request->length);
    if (!request->item) {
      request->status = CALL_NO_SPACE;
    }
  } else if (request->operation == CLIPBOARD_CLEAR) {
    struct clipboard_store *store = (struct clipboard_store *)request->object;
    lock_clipboard();
    struct clipboard_item *previous = store->item;
    store->item = NULL;
    unlock_clipboard();
    if (previous) {
      object_release(&previous->object);
    }
  } else if (request->operation == CLIPBOARD_PASTE) {
    struct clipboard_input input;
    if (!terminal_clipboard_input(request->attachment, &input)) {
      request->status = CALL_WRONG_TYPE;
    } else {
      request->status = admit_paste(request->process->space,
          (struct clipboard_store *)request->object, input.object, 0, &request->transaction_id);
    }
  } else {
    request->status = CALL_BAD_OPERATION;
  }
}

static void local_copy(struct space *space, struct clipboard_store *store)
{
  struct tty *tty = space->tty;
  size_t first, last, length = 0;
  uint64_t geometry;
  size_t anchor, endpoint;
  bool output_locked = log_begin();
  if (!output_locked || !tty->selection_valid || tty->selection_dragging) {
    log_end(output_locked);
    ktrace("clipboard copy: no completed selection\n");
    return;
  }
  geometry = tty->geometry_generation;
  anchor = tty->selection_anchor;
  endpoint = tty->selection_endpoint;
  bool previous_row = false, valid = true;
  for (size_t row = 0; row < tty->height; ++row) {
    if (!tty_selection_row(tty, row, &first, &last)) {
      continue;
    }
    if (previous_row) {
      ++length;
    }
    previous_row = true;
    for (size_t column = first; column <= last; ++column) {
      uint8_t glyph = tty->cells[row * tty->width + column];
      if (glyph < 0x20 || glyph > 0x7e) {
        valid = false;
      }
    }
    size_t end = last + 1;
    while (end > first && tty->cells[row * tty->width + end - 1] == ' ') {
      --end;
    }
    length += end - first;
  }
  log_end(output_locked);
  if (!valid || length > CLIPBOARD_TEXT_MAX) {
    ktrace("clipboard copy: unsupported selection or text limit\n");
    return;
  }
  struct clipboard_item *item = allocate_item(length);
  if (!item) {
    ktrace("clipboard copy: storage unavailable\n");
    return;
  }
  output_locked = log_begin();
  bool same = output_locked && tty->selection_valid && !tty->selection_dragging &&
      geometry == tty->geometry_generation && anchor == tty->selection_anchor &&
      endpoint == tty->selection_endpoint;
  size_t offset = 0;
  previous_row = false;
  if (same) {
    for (size_t row = 0; row < tty->height; ++row) {
      if (!tty_selection_row(tty, row, &first, &last)) {
        continue;
      }
      if (previous_row) {
        item->bytes[offset++] = '\n';
      }
      previous_row = true;
      size_t end = last + 1;
      while (end > first && tty->cells[row * tty->width + end - 1] == ' ') {
        --end;
      }
      memcpy(item->bytes + offset, tty->cells + row * tty->width + first, end - first);
      offset += end - first;
    }
    same = offset == length;
  }
  log_end(output_locked);
  if (!same) {
    object_release(&item->object);
    ktrace("clipboard copy: selection changed\n");
    return;
  }
  lock_clipboard();
  struct clipboard_item *previous = store->item;
  store->item = item;
  unlock_clipboard();
  if (previous) {
    object_release(&previous->object);
  }
  ktrace("clipboard copied %zu bytes (%s)\n", length, store->space ? "local" : "shared");
}

static void queue_kernel_paste(struct space *space, uint64_t layer)
{
  struct clipboard_input input;
  clipboard_input_descriptor(&space->console->object, &input);
  struct pointer_geometry geometry = pointer_surface_geometry(space->terminal_pointer);
  lock_input(&input);
  lock_clipboard();
  struct clipboard_space *state = space->clipboard;
  struct clipboard_receiver *receiver = find_receiver(input.object);
  bool pending = state->activation.id && !state->activation.owner;
  enum call_status refusal = CALL_OK;
  if (!receiver) {
    refusal = CALL_UNAVAILABLE;
  } else if (state->transaction || state->resume_pending || !receiver->boundary ||
      !input_clean(&input, receiver->owner)) {
    refusal = CALL_BUSY;
  }
  bool exhausted = next_action == UINT64_MAX;
  if (!pending && !exhausted) {
    state->activation = (struct clipboard_activation){
      .id = ++next_action, .operation = CLIPBOARD_PASTE, .layer = layer,
      .generation = geometry.generation, .mapping_identity = geometry.mapping_identity,
      .deadline = task_deadline_after_ms(CLIPBOARD_TIMEOUT_MS),
      .receiver_epoch = receiver ? receiver->epoch : 0, .refusal = refusal,
    };
    notify_worker_locked();
  }
  unlock_clipboard();
  unlock_input(&input);
  if (pending || exhausted) {
    ktrace("clipboard paste refused: %u\n", (unsigned)(pending ? CALL_BUSY : CALL_LIMIT));
  }
}

static void consume_kernel_pastes(bool input_complete)
{
  for (struct clipboard_space *state = spaces; state; state = state->next) {
    lock_clipboard();
    struct clipboard_activation action = state->activation;
    bool pending = action.id && !action.owner;
    if (pending) {
      state->activation = (struct clipboard_activation){0};
    }
    unlock_clipboard();
    if (!pending) {
      continue;
    }
    struct space *space = state->space;
    struct pointer_geometry geometry = pointer_surface_geometry(space->terminal_pointer);
    enum call_status status = action.refusal;
    if (space_pointer_active() != space || space->display->visible || space->terminal_pointer->owner ||
        geometry.generation != action.generation || geometry.mapping_identity != action.mapping_identity ||
        arch_monotonic_ns() >= action.deadline) {
      status = CALL_ABANDONED;
    } else if (!input_complete) {
      status = CALL_BUSY;
    } else if (status == CALL_OK) {
      struct clipboard_store *store = action.layer == CLIPBOARD_LAYER_LOCAL ? &state->local : &shared;
      uint64_t transaction_id;
      status = admit_paste(space, store, &space->console->object, action.receiver_epoch, &transaction_id);
    }
    if (status != CALL_OK) {
      ktrace("clipboard paste refused: %u\n", (unsigned)status);
    }
  }
}

bool clipboard_key_event(struct space *space, const struct key_event *event)
{
  KASSERT(arch_cpu_index() == 0);
  if (event->action == KEY_STATE_RESET) {
    memset(command_held, 0, sizeof(command_held));
    lock_clipboard();
    enter_down[0] = enter_down[1] = false;
    unlock_clipboard();
    for (struct clipboard_space *state = spaces; state; state = state->next) {
      clipboard_space_cancel(state->space);
      lock_clipboard();
      state->enter_blocked[0] = state->enter_blocked[1] = true;
      unlock_clipboard();
    }
    return false;
  }
  unsigned enter = event->key == KEY_ENTER ? 0 : 1;
  bool enter_key = event->key == KEY_ENTER || event->key == KEY_KP_ENTER;
  if (enter_key) {
    lock_clipboard();
    enter_down[enter] = event->action != KEY_RELEASE;
    if (event->action == KEY_RELEASE) {
      for (struct clipboard_space *state = spaces; state; state = state->next) {
        state->enter_blocked[enter] = false;
      }
    } else if (space->clipboard->enter_blocked[enter]) {
      unlock_clipboard();
      return true;
    }
    unlock_clipboard();
  }
  if (command_held[event->key]) {
    if (event->action == KEY_RELEASE) {
      command_held[event->key] = false;
    }
    return true;
  }
  lock_clipboard();
  struct clipboard_receiver *receiver = space->clipboard->transaction;
  bool receiver_active = active(receiver);
  bool transaction = receiver_active || space->clipboard->resume_pending;
  unsigned modifiers = event->modifiers &
      (KEY_MOD_SHIFT | KEY_MOD_CONTROL | KEY_MOD_ALT | KEY_MOD_SUPER);
  bool command = event->action == KEY_PRESS && (event->key == KEY_C || event->key == KEY_V) &&
      (modifiers == (KEY_MOD_CONTROL | KEY_MOD_SHIFT) ||
       modifiers == (KEY_MOD_SUPER | KEY_MOD_SHIFT));
  if (transaction) {
    if (receiver_active && event->action == KEY_PRESS && event->key == KEY_C && modifiers == KEY_MOD_CONTROL) {
      receiver->interrupt_pending = true;
      cancel_locked(receiver, CALL_ABANDONED);
    } else if (receiver_active && event->action == KEY_PRESS && event->key == KEY_ESCAPE && !modifiers) {
      cancel_locked(receiver, CALL_ABANDONED);
    }
  }
  unlock_clipboard();
  if (transaction && !command) {
    readiness_notify();
    return true;
  }
  if (space->display->visible || event->action != KEY_PRESS ||
      (event->key != KEY_C && event->key != KEY_V) ||
      (modifiers != (KEY_MOD_CONTROL | KEY_MOD_SHIFT) &&
       modifiers != (KEY_MOD_SUPER | KEY_MOD_SHIFT))) {
    return false;
  }
  command_held[event->key] = true;
  uint64_t operation = event->key == KEY_C ? CLIPBOARD_PUBLISH : CLIPBOARD_PASTE;
  uint64_t layer = modifiers & KEY_MOD_SUPER ? CLIPBOARD_LAYER_SHARED : CLIPBOARD_LAYER_LOCAL;
  struct clipboard_store *store = layer == CLIPBOARD_LAYER_LOCAL ? &space->clipboard->local : &shared;
  struct pointer_object *controller = space->terminal_pointer;
  if (!controller->owner) {
    if (operation == CLIPBOARD_PUBLISH) {
      local_copy(space, store);
    } else if (space != space_caelum()) {
      queue_kernel_paste(space, layer);
    }
    return true;
  }
  if (next_action == UINT64_MAX) {
    return true;
  }
  struct pointer_geometry geometry = pointer_surface_geometry(controller);
  lock_clipboard();
  space->clipboard->activation = (struct clipboard_activation){
    .owner = controller->owner, .id = ++next_action, .operation = operation, .layer = layer,
    .generation = geometry.generation, .mapping_identity = geometry.mapping_identity,
    .deadline = task_deadline_after_ms(CLIPBOARD_TIMEOUT_MS),
  };
  uint64_t id = next_action;
  unlock_clipboard();
  terminal_pointer_clipboard_action(controller, id, operation, layer);
  return true;
}

static void clipboard_worker(void *argument)
{
  (void)argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    uint64_t deadline = UINT64_MAX;
    bool input_complete = space_keyboard_sync_input();
    consume_kernel_pastes(input_complete);
    bool changed = false;
    lock_clipboard();
    for (struct clipboard_receiver *receiver = receivers; receiver; receiver = receiver->next) {
      if (receiver->invalid || !process_control_stopped(receiver->owner->control)) {
        continue;
      }
      cancel_locked(receiver, CALL_ENDPOINT_CLOSED);
      receiver->invalid = true;
      receiver->boundary = false;
      receiver->transaction_id = 0;
      struct clipboard_space *state = receiver->owner->space->clipboard;
      if (state->transaction == receiver) {
        state->transaction = NULL;
        resume_input_locked(state, input_complete);
      }
      wake_receiver(receiver);
      changed = true;
    }
    for (struct clipboard_space *state = spaces; state; state = state->next) {
      if (state->activation.owner && process_control_stopped(state->activation.owner->control)) {
        state->activation = (struct clipboard_activation){0};
      }
      if (state->resume_pending && input_complete) {
        resume_input_locked(state, true);
      } else if (state->resume_pending) {
        uint64_t retry = task_deadline_after_ms(1);
        if (retry < deadline) {
          deadline = retry;
        }
      }
      struct clipboard_receiver *receiver = state->transaction;
      if (!active(receiver) || receiver->phase == CONSOLE_PASTE_CANCEL) {
        continue;
      }
      bool stopped = process_control_stopped(receiver->owner->control);
      struct process *controller = state->space->terminal_pointer->owner;
      stopped |= controller && process_control_stopped(controller->control);
      if (stopped || arch_monotonic_ns() >= receiver->deadline) {
        cancel_locked(receiver, stopped ? CALL_ENDPOINT_CLOSED : CALL_TIMED_OUT);
        if (process_control_stopped(receiver->owner->control)) {
          receiver->invalid = true;
          receiver->transaction_id = 0;
          state->transaction = NULL;
          resume_input_locked(state, input_complete);
        }
        changed = true;
      } else if (receiver->phase != CONSOLE_PASTE_CANCEL && receiver->deadline < deadline) {
        deadline = receiver->deadline;
      }
    }
    if (worker_notified || changed) {
      worker_notified = false;
      unlock_clipboard();
      readiness_notify();
      cpu_restore_interrupts(flags);
      kernel_task_yield_if_runnable();
      continue;
    }
    struct task_wait *wait = task_wait_prepare();
    worker_wait = wait;
    unlock_clipboard();
    if (deadline == UINT64_MAX) {
      task_wait_sleep(wait);
    } else {
      task_wait_sleep_until(wait, deadline);
    }
    lock_clipboard();
    if (worker_wait == wait) {
      worker_wait = NULL;
    }
    unlock_clipboard();
    readiness_notify();
    cpu_restore_interrupts(flags);
  }
}

void clipboard_init(void)
{
  worker_started = true;
  if (kernel_task_create(clipboard_worker, NULL) != MM_OK) {
    panic("cannot create clipboard worker");
  }
}

static enum call_status begin_receiver_read(struct clipboard_receiver *receiver, bool timed,
    uint64_t deadline)
{
  if (receiver->input->type == OBJECT_CONSOLE) {
    return console_paste_begin_read((struct console_object *)receiver->input, timed, deadline);
  }
  return terminal_paste_begin_read(receiver->input, timed, deadline);
}

static void end_receiver_read(struct clipboard_receiver *receiver)
{
  if (receiver->input->type == OBJECT_CONSOLE) {
    console_paste_end_read((struct console_object *)receiver->input);
  } else {
    terminal_paste_end_read(receiver->input);
  }
}

static struct syscall_result receiver_read(struct clipboard_receiver *receiver,
    const struct console_paste_read_request *request, uintptr_t reply_address)
{
  bool timed = request->timeout_ms != CONSOLE_WAIT_FOREVER;
  uint64_t deadline = timed ? task_deadline_after_ms(request->timeout_ms) : 0;
  size_t capacity = request->capacity < CLIPBOARD_READ_CHUNK ? request->capacity : CLIPBOARD_READ_CHUNK;
  if (!capacity || (request->flags & ~CONSOLE_PASTE_BOUNDARY) ||
      (timed && request->timeout_ms > UINT32_MAX)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(request->address, capacity, USER_BUFFER_WRITE) ||
      !user_buffer_check(reply_address, sizeof(struct console_paste_read_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  enum call_status status = begin_receiver_read(receiver, timed, deadline);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct clipboard_input *input = &receiver->target;
  struct console_paste_read_reply reply = {.epoch = receiver->epoch};
  uint8_t bytes[CLIPBOARD_READ_CHUNK];
  lock_clipboard();
  receiver->boundary = !!(request->flags & CONSOLE_PASTE_BOUNDARY);
  unlock_clipboard();
  for (;;) {
    lock_input(input);
    lock_clipboard();
    if (receiver->invalid || task_stop_requested() || (input->hung_up && *input->hung_up)) {
      status = CALL_ENDPOINT_CLOSED;
    } else if (active(receiver)) {
      if (arch_monotonic_ns() >= receiver->deadline && receiver->phase != CONSOLE_PASTE_CANCEL) {
        cancel_locked(receiver, CALL_TIMED_OUT);
      }
      if (!receiver->terminator_delivered) {
        reply.kind = receiver->phase;
        reply.transaction_id = receiver->transaction_id;
        reply.status = receiver->status;
        if (receiver->phase == CONSOLE_PASTE_BEGIN) {
          receiver->phase = receiver->item->length ? CONSOLE_PASTE_DATA : CONSOLE_PASTE_END;
        } else if (receiver->phase == CONSOLE_PASTE_DATA) {
          size_t left = receiver->item->length - receiver->offset;
          reply.length = left < capacity ? left : capacity;
          memcpy(bytes, receiver->item->bytes + receiver->offset, reply.length);
          receiver->offset += reply.length;
          if (receiver->offset == receiver->item->length) {
            receiver->phase = CONSOLE_PASTE_END;
          }
        } else {
          receiver->terminator_delivered = true;
        }
      }
    } else if (input->lost && *input->lost) {
      *input->lost = false;
      status = CALL_INPUT_LOST;
    } else if (*input->count) {
      bytes[0] = input->bytes[*input->head];
      *input->head = (*input->head + 1) % input->capacity;
      --*input->count;
      receiver->boundary = false;
      reply.kind = CONSOLE_PASTE_INPUT;
      reply.length = 1;
    } else if (input->closed && *input->closed) {
      reply.kind = CONSOLE_PASTE_INPUT;
      reply.length = 0;
    }
    if (status != CALL_OK || reply.kind) {
      unlock_clipboard();
      unlock_input(input);
      break;
    }
    if (timed && task_deadline_expired(deadline)) {
      status = CALL_TIMED_OUT;
      unlock_clipboard();
      unlock_input(input);
      break;
    }
    struct task_wait *wait = task_wait_prepare();
    receiver->wait = wait;
    unlock_clipboard();
    unlock_input(input);
    bool resumed = timed ? task_wait_sleep_until_interruptible(wait, deadline) :
        task_wait_sleep_interruptible(wait);
    lock_clipboard();
    if (receiver->wait == wait) {
      receiver->wait = NULL;
    }
    unlock_clipboard();
    if (!resumed) {
      status = CALL_ENDPOINT_CLOSED;
      break;
    }
  }
  end_receiver_read(receiver);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (reply.length) {
    KASSERT(copy_to_user(request->address, bytes, reply.length));
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result clipboard_receiver_call(struct kernel_object *input, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & CONSOLE_RIGHT_READ)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct process *process = process_current();
  struct clipboard_receiver *receiver = process->paste_receiver;
  if (operation == CONSOLE_PASTE_READ) {
    struct console_paste_read_request request = {0};
    if (request_size != sizeof(request) - sizeof(request.header) ||
        reply_capacity < sizeof(struct console_paste_read_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request.epoch, request_address, request_size)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (!receiver || receiver->input != input || receiver->epoch != request.epoch) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return receiver_read(receiver, &request, reply_address);
  }
  uint64_t payload[2] = {0};
  size_t expected = operation == CONSOLE_PASTE_ACK ? 16 : 8;
  if (request_size != expected || (operation == CONSOLE_PASTE_REGISTER &&
      reply_capacity < sizeof(struct console_paste_register_reply))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(payload, request_address, expected) ||
      (operation == CONSOLE_PASTE_REGISTER && !user_buffer_check(reply_address,
       sizeof(struct console_paste_register_reply), USER_BUFFER_WRITE))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct kernel_object *output = NULL;
  uint64_t output_rights;
  if (operation == CONSOLE_PASTE_REGISTER &&
      capability_resolve(&process->capabilities, payload[0], CONSOLE_RIGHT_WRITE, 0,
          &output, &output_rights, NULL) != CAP_OK) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct clipboard_request *request = (void *)bsp_request_prepare(BSP_SERVICE_CLIPBOARD);
  request->process = process;
  request->object = input;
  request->output = output;
  request->operation = operation;
  request->epoch = payload[0];
  request->transaction_id = payload[1];
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  uint64_t epoch = request->epoch;
  bsp_request_release(&request->request);
  if (status == CALL_OK && operation == CONSOLE_PASTE_REGISTER) {
    struct console_paste_register_reply reply = {.epoch = epoch};
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  return (struct syscall_result){status, 0};
}

struct syscall_result clipboard_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required = operation == CLIPBOARD_PASTE ? CLIPBOARD_RIGHT_PASTE :
      operation == CLIPBOARD_REFUSE ? CLIPBOARD_RIGHTS : CLIPBOARD_RIGHT_PUBLISH;
  if (operation < CLIPBOARD_PUBLISH || operation > CLIPBOARD_REFUSE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  enum call_status precheck = (rights & required) ? CALL_OK : CALL_DENIED;
  uint64_t payload[5] = {0};
  size_t expected = operation == CLIPBOARD_PUBLISH ? 40 :
      (operation == CLIPBOARD_PASTE || operation == CLIPBOARD_REFUSE) ? 32 : 24;
  if (request_size != expected) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(payload, request_address, expected)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == CLIPBOARD_PASTE && reply_capacity < sizeof(struct clipboard_paste_reply)) {
    precheck = CALL_BAD_REQUEST;
  } else if (operation == CLIPBOARD_PASTE && !user_buffer_check(reply_address,
      sizeof(struct clipboard_paste_reply), USER_BUFFER_WRITE)) {
    precheck = CALL_BAD_BUFFER;
  }
  if (operation == CLIPBOARD_REFUSE) {
    if (payload[3] != CLIPBOARD_PUBLISH && payload[3] != CLIPBOARD_PASTE) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    uint64_t refuse_right = payload[3] == CLIPBOARD_PASTE ? CLIPBOARD_RIGHT_PASTE : CLIPBOARD_RIGHT_PUBLISH;
    if (!(rights & refuse_right)) {
      precheck = CALL_DENIED;
    }
  }
  struct process *process = process_current();
  struct kernel_object *attachment = NULL;
  if (operation == CLIPBOARD_PASTE && capability_resolve(&process->capabilities,
      payload[3], TERMINAL_RIGHT_INJECT, 0, &attachment, NULL, NULL) != CAP_OK) {
    precheck = CALL_DENIED;
  }
  if (operation == CLIPBOARD_PUBLISH && (payload[4] > CLIPBOARD_TEXT_MAX ||
      !user_buffer_check(payload[3], payload[4], USER_BUFFER_READ))) {
    precheck = payload[4] > CLIPBOARD_TEXT_MAX ? CALL_LIMIT : CALL_BAD_BUFFER;
  }
  struct clipboard_request *request = (void *)bsp_request_prepare(BSP_SERVICE_CLIPBOARD);
  request->process = process;
  request->object = object;
  request->attachment = attachment;
  request->operation = operation == CLIPBOARD_PUBLISH ? CLIPBOARD_ALLOCATE : operation;
  request->action_id = payload[0];
  request->generation = payload[1];
  request->mapping_identity = payload[2];
  request->length = operation == CLIPBOARD_REFUSE ? payload[3] : payload[4];
  request->refusal = precheck;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  uint64_t transaction_id = request->transaction_id;
  struct clipboard_item *item = operation == CLIPBOARD_PUBLISH && status == CALL_OK ? request->item : NULL;
  bsp_request_release(&request->request);
  if (item) {
    KASSERT(copy_from_user(item->bytes, payload[3], item->length));
    /* text/plain is ASCII for this slice. Normalize CRLF/CR before publication. */
    size_t output = 0;
    for (size_t i = 0; i < item->length; ++i) {
      uint8_t byte = item->bytes[i];
      if (byte == '\r') {
        if (i + 1 < item->length && item->bytes[i + 1] == '\n') {
          ++i;
        }
        byte = '\n';
      }
      if ((byte < 0x20 && byte != '\n' && byte != '\t') || byte > 0x7e) {
        status = CALL_BAD_REQUEST;
        break;
      }
      item->bytes[output++] = byte;
    }
    /* Retained accounting charges the allocation, not merely normalized length. */
    if (status == CALL_OK) {
      request = (void *)bsp_request_prepare(BSP_SERVICE_CLIPBOARD);
      request->process = process;
      request->object = object;
      request->operation = CLIPBOARD_COMMIT;
      request->item = item;
      request->generation = payload[1];
      request->mapping_identity = payload[2];
      request->length = output;
      bsp_request_submit_and_wait(&request->request);
      status = request->status;
      item = request->item;
      bsp_request_release(&request->request);
    }
    if (item) {
      object_release(&item->object);
    }
  }
  if (status == CALL_OK && operation == CLIPBOARD_PASTE) {
    struct clipboard_paste_reply reply = {.transaction_id = transaction_id};
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  return (struct syscall_result){status, 0};
}

struct syscall_result clipboard_controller_refuse_call(uintptr_t request_address, size_t request_size)
{
  uint64_t payload[4];
  if (request_size != sizeof(payload)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(payload, request_address, sizeof(payload))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (payload[3] != CLIPBOARD_PUBLISH && payload[3] != CLIPBOARD_PASTE) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct clipboard_request *request = (void *)bsp_request_prepare(BSP_SERVICE_CLIPBOARD);
  request->process = process_current();
  request->operation = CLIPBOARD_CONTROLLER_REFUSE;
  request->action_id = payload[0];
  request->generation = payload[1];
  request->mapping_identity = payload[2];
  request->length = payload[3];
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  bsp_request_release(&request->request);
  return (struct syscall_result){status, 0};
}
