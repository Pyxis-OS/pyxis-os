#include <kernel/object/clipboard.h>
#include <abi/console.h>
#include <abi/wait.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/capability.h>
#include <kernel/object/console.h>
#include <kernel/object/terminal.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user_memory.h>
#include <kernel/wait.h>

struct terminal_session;

struct terminal_end {
  struct kernel_object object;
  struct terminal_session *session;
};

struct terminal_session {
  atomic_bool locked;
  struct space *space;
  struct process *reader_process;
  struct terminal_end input, output, attachment, events;
  size_t live_objects, input_authorities, controllers;
  /* Each output-producing object may retire while the other remains open. */
  size_t output_authorities, event_authorities;
  uint64_t columns, rows, geometry_generation, completed_commands;
  struct console_interrupt interrupt;
  bool input_closed, output_closed, hung_up, reader_active;
  struct task_wait_link *first_reader, *last_reader, *writers;
  struct task_wait *input_wait;
  size_t input_head, input_count, output_head, output_count;
  uint8_t input_data[TERMINAL_INPUT_CAPACITY];
  uint8_t output_data[TERMINAL_OUTPUT_CAPACITY];
};

/* IF=0; session -> scheduler locks. Only bounded copies of kernel buffers
 * while held: no allocation, user access, readiness notification or context switch. */
static void lock_session(struct terminal_session *session)
{
  while (atomic_exchange_explicit(&session->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_session(struct terminal_session *session)
{
  atomic_store_explicit(&session->locked, false, memory_order_release);
}

static void wake_input(struct terminal_session *session)
{
  struct task_wait *wait = session->input_wait;
  session->input_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void wake_all(struct task_wait_link **queue)
{
  struct task_wait_link *record = *queue;
  *queue = NULL;
  while (record) {
    struct task_wait_link *next = record->next;
    struct task_wait *wait = record->wait;
    record->next = NULL;
    record->wait = NULL;
    task_wait_wake(wait);
    record = next;
  }
}

static void hangup(struct terminal_session *session)
{
  clipboard_input_hangup(&session->input.object);
  session->hung_up = true;
  session->input_closed = session->output_closed = true;
  session->input_head = session->input_count = 0;
  session->output_head = session->output_count = 0;
  wake_input(session);
  struct task_wait_link *reader = session->first_reader;
  session->first_reader = NULL;
  session->last_reader = NULL;
  while (reader) {
    struct task_wait_link *next = reader->next;
    struct task_wait *wait = reader->wait;
    reader->next = NULL;
    /* Keep wait non-NULL: closure did not hand this reader ownership. */
    task_wait_wake(wait);
    reader = next;
  }
  wake_all(&session->writers);
}

static size_t *authority_counter(struct kernel_object *object, uint64_t rights)
{
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  switch (object->type) {
  case OBJECT_TERMINAL_INPUT:
    return rights & CONSOLE_RIGHT_READ ? &session->input_authorities : NULL;
  case OBJECT_TERMINAL_OUTPUT:
    return rights & CONSOLE_RIGHT_WRITE ? &session->output_authorities : NULL;
  case OBJECT_TERMINAL_EVENTS:
    return rights & TERMINAL_EVENTS_RIGHT_EMIT ? &session->event_authorities : NULL;
  case OBJECT_TERMINAL_ATTACHMENT:
    return rights & TERMINAL_RIGHT_HANGUP ? &session->controllers : NULL;
  default:
    return NULL;
  }
}

static bool terminal_end_type(enum object_type type)
{
  return type == OBJECT_TERMINAL_INPUT || type == OBJECT_TERMINAL_OUTPUT ||
      type == OBJECT_TERMINAL_ATTACHMENT || type == OBJECT_TERMINAL_EVENTS;
}

bool terminal_authority_retain(struct kernel_object *object, uint64_t rights)
{
  if (!terminal_end_type(object->type)) {
    return true;
  }
  if (object->type == OBJECT_TERMINAL_INPUT && !(rights & CONSOLE_RIGHT_READ)) {
    return console_interrupt_retain(&((struct terminal_end *)object)->session->interrupt,
        rights);
  }
  size_t *counter = authority_counter(object, rights);
  if (!counter) {
    return true;
  }
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  lock_session(session);
  bool retained = *counter != SIZE_MAX;
  if (retained) {
    ++*counter;
  }
  unlock_session(session);
  return retained;
}

void terminal_authority_release(struct kernel_object *object, uint64_t rights)
{
  if (!terminal_end_type(object->type)) {
    return;
  }
  if (object->type == OBJECT_TERMINAL_INPUT && !(rights & CONSOLE_RIGHT_READ)) {
    console_interrupt_release(&((struct terminal_end *)object)->session->interrupt, rights);
    return;
  }
  size_t *counter = authority_counter(object, rights);
  if (!counter) {
    return;
  }
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  lock_session(session);
  KASSERT(*counter);
  bool closed = --*counter == 0;
  if (closed) {
    if (object->type == OBJECT_TERMINAL_INPUT) {
      clipboard_input_hangup(&session->input.object);
      session->input_closed = true;
      session->input_head = session->input_count = 0;
      wake_input(session);
    } else if (object->type == OBJECT_TERMINAL_OUTPUT ||
        object->type == OBJECT_TERMINAL_EVENTS) {
      if (!session->output_authorities && !session->event_authorities) {
        session->output_closed = true;
        wake_all(&session->writers);
      }
    } else {
      hangup(session);
    }
  }
  unlock_session(session);
  if (closed) {
    readiness_notify();
  }
}

static void destroy_terminal_end(struct kernel_object *object)
{
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  lock_session(session);
  KASSERT(!*authority_counter(object, object->type == OBJECT_TERMINAL_ATTACHMENT ?
      TERMINAL_RIGHT_HANGUP : object->type == OBJECT_TERMINAL_EVENTS ?
      TERMINAL_EVENTS_RIGHT_EMIT : CONSOLE_RIGHTS));
  KASSERT(session->live_objects);
  bool finished = --session->live_objects == 0;
  if (finished) {
    KASSERT(!session->reader_active && !session->first_reader &&
        !session->writers && !session->input_wait);
    KASSERT(!session->interrupt.armed && !session->interrupt.passthrough);
  }
  unlock_session(session);
  if (finished) {
    kfree(session);
  }
}

static struct terminal_session *session_create(uint64_t columns, uint64_t rows)
{
  struct terminal_session *session = kmalloc(sizeof(*session));
  if (!session) {
    return NULL;
  }
  memset(session, 0, sizeof(*session));
  atomic_init(&session->locked, false);
  atomic_init(&session->interrupt.locked, false);
  session->columns = columns;
  session->rows = rows;
  session->geometry_generation = 1;
  session->live_objects = 4;
  session->input.session = session->output.session = session->attachment.session = session;
  session->events.session = session;
  object_init(&session->input.object, OBJECT_TERMINAL_INPUT, destroy_terminal_end);
  object_init(&session->output.object, OBJECT_TERMINAL_OUTPUT, destroy_terminal_end);
  object_init(&session->attachment.object, OBJECT_TERMINAL_ATTACHMENT, destroy_terminal_end);
  object_init(&session->events.object, OBJECT_TERMINAL_EVENTS, destroy_terminal_end);
  return session;
}

void terminal_create_execute(struct terminal_create_service_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->table);
  struct terminal_session *session = session_create(request->columns, request->rows);
  if (session) {
    session->space = request->space;
  }
  request->result = CALL_NO_MEMORY;
  if (session) {
    struct kernel_object *objects[] = {
      &session->input.object, &session->output.object, &session->attachment.object,
      &session->events.object,
    };
    const uint64_t rights[] = {
      CONSOLE_RIGHT_READ | CONSOLE_RIGHT_INTERRUPT, CONSOLE_RIGHT_WRITE, TERMINAL_RIGHTS,
      TERMINAL_EVENTS_RIGHT_EMIT,
    };
    const uint64_t transport[] = {0, 0, 0, 0};
    handle_t handles[4];
    enum capability_result result;
    while ((result = capability_insert_batch(request->table, objects, rights,
        transport, 4, handles)) == CAP_FULL) {
      result = capability_grow(request->table);
      if (result != CAP_OK) {
        break;
      }
    }
    if (result == CAP_OK) {
      request->reply = (struct terminal_create_reply){
        .input = handles[0], .output = handles[1], .attachment = handles[2], .events = handles[3],
      };
      request->result = CALL_OK;
    } else {
      KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT);
      request->result = result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
    }
    for (size_t i = 0; i < 4; ++i) {
      object_release(objects[i]);
    }
  }
  request->table = NULL;
}

static void destroy_terminal_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *terminal_service_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_TERMINAL_SERVICE, destroy_terminal_service);
  }
  return object;
}

struct syscall_result terminal_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != TERMINAL_CREATE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & TERMINAL_SERVICE_RIGHT_CREATE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct terminal_create_request input = {0};
  size_t payload_size = sizeof(input) - sizeof(input.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct terminal_create_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&input.columns, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct terminal_create_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!input.columns || input.columns > TERMINAL_COLUMNS_MAX ||
      !input.rows || input.rows > TERMINAL_ROWS_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct terminal_create_service_request *request = (void *)
      bsp_request_prepare(BSP_SERVICE_TERMINAL_CREATE);
  struct process *process = process_current();
  KASSERT(process);
  request->table = &process->capabilities;
  request->space = process_current()->space;
  request->columns = input.columns;
  request->rows = input.rows;
  request->reply = (struct terminal_create_reply){0};
  request->result = CALL_NO_MEMORY;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  struct terminal_create_reply reply = request->reply;
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static void ring_copy_out(const uint8_t *ring, size_t capacity, size_t head,
    void *destination, size_t length)
{
  size_t first = capacity - head;
  if (first > length) {
    first = length;
  }
  memcpy(destination, ring + head, first);
  memcpy((uint8_t *)destination + first, ring, length - first);
}

static void ring_copy_in(uint8_t *ring, size_t capacity, size_t head,
    const void *source, size_t length)
{
  size_t first = capacity - head;
  if (first > length) {
    first = length;
  }
  memcpy(ring + head, source, first);
  memcpy(ring, (const uint8_t *)source + first, length - first);
}

static void end_read(struct terminal_session *session);

static enum call_status begin_read(struct terminal_session *session,
    bool timed, uint64_t deadline, bool paste)
{
  lock_session(session);
  if (!paste && !clipboard_ordinary_allowed(&session->input.object)) {
    unlock_session(session);
    return CALL_BUSY;
  }
  if (session->hung_up || task_stop_requested()) {
    unlock_session(session);
    return CALL_ENDPOINT_CLOSED;
  }
  if (!session->reader_active) {
    session->reader_active = true;
    session->reader_process = process_current();
    unlock_session(session);
    return CALL_OK;
  }
  if (timed && task_deadline_expired(deadline)) {
    unlock_session(session);
    return CALL_TIMED_OUT;
  }
  struct task_wait_link *reader = task_wait_link_prepare();
  reader->reader_process = process_current();
  struct task_wait *wait = reader->wait;
  if (session->last_reader) {
    session->last_reader->next = reader;
  } else {
    session->first_reader = reader;
  }
  session->last_reader = reader;
  unlock_session(session);
  bool resumed = timed ? task_wait_sleep_until_interruptible(wait, deadline) :
      task_wait_sleep_interruptible(wait);
  lock_session(session);
  bool acquired = reader->wait == NULL;
  if (session->hung_up) {
    if (acquired) {
      /* The previous reader handed ownership to us before hangup won. */
      session->reader_active = false;
    }
    reader->wait = NULL;
    unlock_session(session);
    return CALL_ENDPOINT_CLOSED;
  }
  if (!acquired) {
    struct task_wait_link **link = &session->first_reader;
    struct task_wait_link *previous = NULL;
    while (*link != reader) {
      KASSERT(*link);
      previous = *link;
      link = &(*link)->next;
    }
    *link = reader->next;
    if (session->last_reader == reader) {
      session->last_reader = previous;
    }
    reader->next = NULL;
    reader->wait = NULL;
  }
  bool stopped = !resumed || task_stop_requested();
  unlock_session(session);
  if (stopped) {
    if (acquired) {
      end_read(session);
    }
    return CALL_ENDPOINT_CLOSED;
  }
  return acquired ? CALL_OK : CALL_TIMED_OUT;
}

static void end_read(struct terminal_session *session)
{
  lock_session(session);
  struct task_wait_link *reader = session->first_reader;
  if (reader) {
    session->first_reader = reader->next;
    if (!session->first_reader) {
      session->last_reader = NULL;
    }
    struct task_wait *wait = reader->wait;
    reader->next = NULL;
    reader->wait = NULL;
    session->reader_process = reader->reader_process;
    task_wait_wake(wait);
  } else {
    session->reader_active = false;
    session->reader_process = NULL;
  }
  unlock_session(session);
  clipboard_input_notify(&session->input.object);
  readiness_notify();
}

static struct syscall_result application_read(struct terminal_session *session,
    const struct console_read_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  bool timed = request->timeout_ms != CONSOLE_WAIT_FOREVER;
  if ((timed && request->timeout_ms > UINT32_MAX) ||
      reply_capacity < sizeof(struct console_read_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t deadline = timed ? task_deadline_after_ms(request->timeout_ms) : 0;
  size_t capacity = request->capacity < TERMINAL_TRANSFER_MAX ?
      request->capacity : TERMINAL_TRANSFER_MAX;
  if (!user_buffer_check(request->address, capacity, USER_BUFFER_WRITE) ||
      !user_buffer_check(reply_address, sizeof(struct console_read_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct console_read_reply reply = {0};
  uint8_t bytes[TERMINAL_TRANSFER_MAX];
  if (capacity) {
    enum call_status status = begin_read(session, timed, deadline, false);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
    lock_session(session);
    if (!clipboard_ordinary_allowed(&session->input.object)) {
      unlock_session(session);
      end_read(session);
      return (struct syscall_result){CALL_BUSY, 0};
    }
    while (!session->hung_up && !session->input_count && !session->input_closed) {
      if (timed && task_deadline_expired(deadline)) {
        unlock_session(session);
        end_read(session);
        return (struct syscall_result){CALL_TIMED_OUT, 0};
      }
      struct task_wait *wait = task_wait_prepare();
      session->input_wait = wait;
      unlock_session(session);
      bool resumed = timed ? task_wait_sleep_until_interruptible(wait, deadline) :
          task_wait_sleep_interruptible(wait);
      lock_session(session);
      if (session->input_wait == wait) {
        session->input_wait = NULL;
      }
      if (!resumed || task_stop_requested()) {
        unlock_session(session);
        end_read(session);
        return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
      }
    }
    if (session->hung_up) {
      unlock_session(session);
      end_read(session);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
    if (!clipboard_ordinary_allowed(&session->input.object)) {
      unlock_session(session);
      end_read(session);
      return (struct syscall_result){CALL_BUSY, 0};
    }
    clipboard_ordinary_transfer(&session->input.object);
    reply.read = session->input_count < capacity ? session->input_count : capacity;
    ring_copy_out(session->input_data, TERMINAL_INPUT_CAPACITY,
        session->input_head, bytes, reply.read);
    session->input_head = (session->input_head + reply.read) % TERMINAL_INPUT_CAPACITY;
    session->input_count -= reply.read;
    unlock_session(session);
    readiness_notify();
    KASSERT(copy_to_user(request->address, bytes, reply.read));
    end_read(session);
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static enum call_status enqueue_output(struct terminal_session *session,
    uint64_t type, const void *bytes, size_t length, size_t *written, bool try)
{
  *written = 0;
  for (;;) {
    struct task_wait_link *writer = try ? NULL : task_wait_link_prepare();
    lock_session(session);
    if (task_stop_requested() || session->hung_up || session->output_closed) {
      unlock_session(session);
      return CALL_ENDPOINT_CLOSED;
    }
    if (type == TERMINAL_RECORD_COMMAND_COMPLETE && session->completed_commands == UINT64_MAX) {
      unlock_session(session);
      return CALL_LIMIT;
    }
    /* A blocking write waits until its whole record fits. Admitting the space
     * a drain just freed would split a continuous writer into tiny records; the
     * drainer then pays one round per fragment. Writes are at most
     * TERMINAL_TRANSFER_MAX bytes, a small part of the queue. */
    size_t available = TERMINAL_OUTPUT_CAPACITY - session->output_count;
    size_t required = sizeof(struct terminal_record) + length;
    if (try && available > sizeof(struct terminal_record)) {
      size_t capacity = available - sizeof(struct terminal_record);
      if (length > capacity) {
        length = capacity;
        required = sizeof(struct terminal_record) + length;
      }
    }
    if (available >= required) {
      size_t count = length;
      struct terminal_record record = {.type = type, .length = count};
      size_t tail = (session->output_head + session->output_count) % TERMINAL_OUTPUT_CAPACITY;
      ring_copy_in(session->output_data, TERMINAL_OUTPUT_CAPACITY, tail, &record, sizeof(record));
      tail = (tail + sizeof(record)) % TERMINAL_OUTPUT_CAPACITY;
      if (type == TERMINAL_RECORD_COMMAND_COMPLETE) {
        /* Number assignment and the whole record share this queue insertion. */
        const struct terminal_command_complete *completion = bytes;
        struct terminal_command_complete payload = {
          .command = session->completed_commands + 1,
          .kind = completion->kind, .status = completion->status,
        };
        ring_copy_in(session->output_data, TERMINAL_OUTPUT_CAPACITY, tail, &payload, sizeof(payload));
        session->completed_commands = payload.command;
      } else {
        ring_copy_in(session->output_data, TERMINAL_OUTPUT_CAPACITY, tail, bytes, count);
      }
      session->output_count += sizeof(record) + count;
      *written = count;
      unlock_session(session);
      readiness_notify();
      return CALL_OK;
    }
    if (try) {
      unlock_session(session);
      return CALL_WOULD_BLOCK;
    }
    writer->next = session->writers;
    session->writers = writer;
    struct task_wait *wait = writer->wait;
    unlock_session(session);
    bool resumed = task_wait_sleep_interruptible(wait);
    lock_session(session);
    if (writer->wait) {
      struct task_wait_link **link = &session->writers;
      while (*link != writer) {
        KASSERT(*link);
        link = &(*link)->next;
      }
      *link = writer->next;
      writer->next = NULL;
      writer->wait = NULL;
    }
    bool stopped = !resumed || task_stop_requested();
    unlock_session(session);
    if (stopped) {
      return CALL_ENDPOINT_CLOSED;
    }
  }
}

static struct syscall_result application_write(struct terminal_session *session,
    const struct console_write_request *request, uintptr_t reply_address,
    size_t reply_capacity, bool try)
{
  if (reply_capacity < sizeof(struct console_write_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  size_t length = request->length < TERMINAL_TRANSFER_MAX ? request->length : TERMINAL_TRANSFER_MAX;
  uint8_t bytes[TERMINAL_TRANSFER_MAX];
  if (!user_buffer_check(reply_address, sizeof(struct console_write_reply), USER_BUFFER_WRITE) ||
      !copy_from_user(bytes, request->address, length)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t written = 0;
  if (length) {
    enum call_status status = enqueue_output(session, TERMINAL_RECORD_DATA,
        bytes, length, &written, try);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
  }
  struct console_write_reply reply = {.written = written};
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result terminal_application_call(struct kernel_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (operation >= CONSOLE_PASTE_REGISTER && operation <= CONSOLE_PASTE_RELEASE) {
    return clipboard_receiver_call(object, rights, operation, request_address,
        request_size, reply_address, reply_capacity);
  }
  uint64_t required;
  switch (operation) {
  case CONSOLE_READ: required = CONSOLE_RIGHT_READ; break;
  case CONSOLE_WRITE:
  case CONSOLE_TRY_WRITE:
  case CONSOLE_FRESH_LINE:
  case CONSOLE_SET_TAB_WIDTH: required = CONSOLE_RIGHT_WRITE; break;
  case CONSOLE_SIZE: required = CONSOLE_RIGHTS; break;
  case CONSOLE_ARM_INTERRUPT: required = CONSOLE_RIGHT_INTERRUPT; break;
  case CONSOLE_PASSTHROUGH: required = CONSOLE_RIGHT_READ; break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  union console_payload request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  if (operation == CONSOLE_ARM_INTERRUPT || operation == CONSOLE_PASSTHROUGH) {
    return console_interrupt_call(object, &session->interrupt, operation,
        reply_address, reply_capacity);
  }
  if (operation == CONSOLE_READ) {
    return application_read(session, &request.read, reply_address, reply_capacity);
  }
  if (operation == CONSOLE_WRITE || operation == CONSOLE_TRY_WRITE) {
    return application_write(session, &request.write, reply_address, reply_capacity,
        operation == CONSOLE_TRY_WRITE);
  }
  if (operation == CONSOLE_SIZE) {
    if (reply_capacity < sizeof(struct console_size_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    lock_session(session);
    struct console_size_reply reply = {
      .columns = session->columns, .rows = session->rows,
      .generation = session->geometry_generation,
    };
    unlock_session(session);
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  uint64_t type = TERMINAL_RECORD_FRESH_LINE;
  size_t length = 0;
  if (operation == CONSOLE_SET_TAB_WIDTH) {
    if (request.tab_width.columns < CONSOLE_TAB_WIDTH_MIN ||
        request.tab_width.columns > CONSOLE_TAB_WIDTH_MAX) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    type = TERMINAL_RECORD_TAB_WIDTH;
    length = sizeof(request.tab_width.columns);
  }
  size_t written;
  enum call_status status = enqueue_output(session, type, &request.tab_width.columns,
      length, &written, false);
  return (struct syscall_result){status, 0};
}

static bool valid_completion(uint64_t kind, int64_t status)
{
  switch (kind) {
  case TERMINAL_COMPLETION_EXITED:
    return status >= INT32_MIN && status <= INT32_MAX;
  case TERMINAL_COMPLETION_BUILTIN:
    return status == 0 || status == 1;
  case TERMINAL_COMPLETION_FAULTED:
  case TERMINAL_COMPLETION_TERMINATED:
  case TERMINAL_COMPLETION_LAUNCH_FAILED:
  case TERMINAL_COMPLETION_REJECTED:
  case TERMINAL_COMPLETION_LAUNCHED:
    return status == 0;
  default:
    return false;
  }
}

struct syscall_result terminal_events_call(struct kernel_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size)
{
  if (operation != TERMINAL_COMMAND_COMPLETE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & TERMINAL_EVENTS_RIGHT_EMIT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct terminal_command_complete_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.kind, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!valid_completion(request.kind, request.status)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct terminal_command_complete completion = {.kind = request.kind, .status = request.status};
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  size_t written;
  enum call_status status = enqueue_output(session, TERMINAL_RECORD_COMMAND_COMPLETE,
      &completion, sizeof(completion), &written, false);
  return (struct syscall_result){status, 0};
}

static bool buffers_overlap(uintptr_t first, size_t first_size,
    uintptr_t second, size_t second_size)
{
  return first_size && second_size && first < second + second_size && second < first + first_size;
}

static struct syscall_result attachment_transfer(struct terminal_session *session,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct terminal_transfer_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct terminal_transfer_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.buffer, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct terminal_transfer_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  uint8_t bytes[TERMINAL_RECORD_MAX];
  struct terminal_transfer_reply reply = {0};
  bool inject = operation == TERMINAL_TRY_INJECT;
  size_t length = request.length;
  if (inject) {
    if (length > TERMINAL_TRANSFER_MAX) {
      length = TERMINAL_TRANSFER_MAX;
    }
    if (!copy_from_user(bytes, request.buffer, length)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  } else {
    if (length < sizeof(struct terminal_record)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (length > sizeof(bytes)) {
      length = sizeof(bytes);
    }
    if (!user_buffer_check(request.buffer, length, USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (buffers_overlap(request.buffer, length, reply_address, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }
  enum call_status status = CALL_OK;
  lock_session(session);
  if (inject && !length) {
    /* A validated no-op does not probe session liveness. */
  } else if (session->hung_up) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (inject && !clipboard_ordinary_allowed(&session->input.object)) {
    status = CALL_BUSY;
  } else if (inject) {
    /* A recognized Ctrl+C precedes the capacity check: it discards queued input
     * and its prefix, which count as accepted. */
    size_t consumed = session->input_closed ? 0 :
        console_interrupt_scan(&session->interrupt, bytes, length);
    if (consumed) {
      session->input_head = session->input_count = 0;
    }
    if (session->input_closed) {
      status = CALL_ENDPOINT_CLOSED;
    } else if (session->input_count == TERMINAL_INPUT_CAPACITY) {
      status = CALL_WOULD_BLOCK;
    } else {
      size_t available = TERMINAL_INPUT_CAPACITY - session->input_count;
      size_t queued = length - consumed < available ? length - consumed : available;
      size_t tail = (session->input_head + session->input_count) % TERMINAL_INPUT_CAPACITY;
      ring_copy_in(session->input_data, TERMINAL_INPUT_CAPACITY, tail, bytes + consumed, queued);
      session->input_count += queued;
      reply.length = consumed + queued;
      if (queued) {
        wake_input(session);
      }
    }
  } else if (session->output_count) {
    struct terminal_record record;
    ring_copy_out(session->output_data, TERMINAL_OUTPUT_CAPACITY,
        session->output_head, &record, sizeof(record));
    KASSERT(record.length <= TERMINAL_TRANSFER_MAX);
    size_t record_size = sizeof(record) + record.length;
    KASSERT(record_size <= session->output_count);
    if (record_size > length) {
      status = CALL_BUFFER_TOO_SMALL;
    } else {
      reply.length = record_size;
      ring_copy_out(session->output_data, TERMINAL_OUTPUT_CAPACITY,
          session->output_head, bytes, record_size);
      session->output_head = (session->output_head + record_size) % TERMINAL_OUTPUT_CAPACITY;
      session->output_count -= record_size;
      wake_all(&session->writers);
    }
  } else if (!session->output_closed) {
    status = CALL_WOULD_BLOCK;
  }
  unlock_session(session);
  clipboard_input_notify(&session->input.object);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (reply.length) {
    readiness_notify();
  }
  if (!inject) {
    KASSERT(copy_to_user(request.buffer, bytes, reply.length));
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result attachment_resize(struct terminal_session *session,
    uintptr_t request_address, size_t request_size)
{
  struct terminal_resize_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.columns, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!request.columns || request.columns > TERMINAL_COLUMNS_MAX ||
      !request.rows || request.rows > TERMINAL_ROWS_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  enum call_status status = CALL_OK;
  bool changed = false;
  lock_session(session);
  if (session->hung_up) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (request.columns != session->columns || request.rows != session->rows) {
    if (session->geometry_generation == UINT64_MAX) {
      status = CALL_LIMIT;
    } else {
      session->columns = request.columns;
      session->rows = request.rows;
      ++session->geometry_generation;
      changed = true;
    }
  }
  unlock_session(session);
  if (changed) {
    readiness_notify();
  }
  return (struct syscall_result){status, 0};
}

struct syscall_result terminal_attachment_call(struct kernel_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case TERMINAL_TRY_INJECT:
  case TERMINAL_END_INPUT: required = TERMINAL_RIGHT_INJECT; break;
  case TERMINAL_TRY_DRAIN: required = TERMINAL_RIGHT_DRAIN; break;
  case TERMINAL_HANGUP: required = TERMINAL_RIGHT_HANGUP; break;
  case TERMINAL_RESIZE: required = TERMINAL_RIGHT_RESIZE; break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  if (operation == TERMINAL_RESIZE) {
    return attachment_resize(session, request_address, request_size);
  }
  if (operation == TERMINAL_TRY_INJECT || operation == TERMINAL_TRY_DRAIN) {
    return attachment_transfer(session, operation, request_address,
        request_size, reply_address, reply_capacity);
  }
  if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  lock_session(session);
  if (operation == TERMINAL_HANGUP) {
    hangup(session);
  } else {
    /* EOF preserves queued reads and their receiver; only hangup invalidates it. */
    session->input_closed = true;
    wake_input(session);
  }
  unlock_session(session);
  clipboard_input_notify(&session->input.object);
  readiness_notify();
  return (struct syscall_result){CALL_OK, 0};
}

uint64_t terminal_application_ready(struct kernel_object *object, uint64_t events,
    uint64_t observed_generation, struct process *caller)
{
  KASSERT(object->type == OBJECT_TERMINAL_INPUT || object->type == OBJECT_TERMINAL_OUTPUT);
  uint64_t flags = cpu_save_interrupts();
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  lock_session(session);
  uint64_t ready = session->hung_up ? WAIT_ERROR :
      (events & WAIT_READABLE) ? clipboard_input_ready(object, caller) : 0;
  if ((events & WAIT_READABLE) && !session->reader_active && session->input_count && clipboard_ordinary_allowed(&session->input.object)) {
    ready |= WAIT_READABLE;
  }
  if ((events & (WAIT_READABLE | WAIT_PEER_FIN)) && session->input_closed) {
    ready |= WAIT_PEER_FIN;
  }
  if ((events & WAIT_WRITABLE) && !session->hung_up && !session->output_closed &&
      TERMINAL_OUTPUT_CAPACITY - session->output_count > sizeof(struct terminal_record)) {
    ready |= WAIT_WRITABLE;
  }
  if ((events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED)) && session->output_closed) {
    ready |= WAIT_WRITE_CLOSED;
  }
  if (events & WAIT_INTERRUPT) {
    ready |= console_interrupt_ready(&session->interrupt);
  }
  if ((events & WAIT_RESIZED) && observed_generation != session->geometry_generation) {
    ready |= WAIT_RESIZED;
  }
  unlock_session(session);
  cpu_restore_interrupts(flags);
  return ready;
}

uint64_t terminal_attachment_ready(struct kernel_object *object, uint64_t events)
{
  KASSERT(object->type == OBJECT_TERMINAL_ATTACHMENT);
  uint64_t flags = cpu_save_interrupts();
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  lock_session(session);
  uint64_t ready = 0;
  if (session->hung_up) {
    ready |= WAIT_ERROR;
  }
  if ((events & WAIT_READABLE) && session->output_count) {
    ready |= WAIT_READABLE;
  }
  if ((events & (WAIT_READABLE | WAIT_PEER_FIN)) && session->output_closed) {
    ready |= WAIT_PEER_FIN;
  }
  if ((events & WAIT_WRITABLE) && !session->input_closed &&
      session->input_count < TERMINAL_INPUT_CAPACITY &&
      clipboard_ordinary_allowed(&session->input.object)) {
    ready |= WAIT_WRITABLE;
  }
  if ((events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED)) && session->input_closed) {
    ready |= WAIT_WRITE_CLOSED;
  }
  unlock_session(session);
  cpu_restore_interrupts(flags);
  return ready;
}

bool terminal_clipboard_input(struct kernel_object *object, struct clipboard_input *input)
{
  if (!object || (object->type != OBJECT_TERMINAL_INPUT && object->type != OBJECT_TERMINAL_ATTACHMENT)) {
    return false;
  }
  struct terminal_session *session = ((struct terminal_end *)object)->session;
  *input = (struct clipboard_input){
    .object = &session->input.object, .output = &session->output.object, .space = session->space,
    .locked = &session->locked, .bytes = session->input_data, .capacity = TERMINAL_INPUT_CAPACITY,
    .head = &session->input_head, .count = &session->input_count, .closed = &session->input_closed,
    .hung_up = &session->hung_up, .reader_active = &session->reader_active,
    .reader_process = &session->reader_process, .first_reader = &session->first_reader,
    .input_wait = &session->input_wait, .interrupt = &session->interrupt,
  };
  return true;
}

enum call_status terminal_paste_begin_read(struct kernel_object *object, bool timed, uint64_t deadline)
{
  return begin_read(((struct terminal_end *)object)->session, timed, deadline, true);
}

void terminal_paste_end_read(struct kernel_object *object)
{
  end_read(((struct terminal_end *)object)->session);
}
