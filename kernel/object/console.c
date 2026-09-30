#include <abi/console.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <kernel/keyboard.h>
#include <kernel/task.h>
#include <kernel/object/console.h>
#include <kernel/user_memory.h>
#include <kernel/fb/tty.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>

#define CONSOLE_WRITE_CHUNK 256
#define CONSOLE_READ_CHUNK 256

static void destroy_console(struct kernel_object *object)
{
  /* The reference header is the first member; only the wrapper is owned. */
  struct console_object *console = (struct console_object *)object;
  KASSERT(!console->reader_active && !console->first_reader && !console->input_wait);
  kfree(console);
}

struct console_object *console_create(struct tty *tty)
{
  KASSERT(arch_cpu_index() == 0 && tty && tty->initialized);
  struct console_object *console = kmalloc(sizeof(*console));
  if (!console) {
    return NULL;
  }
  *console = (struct console_object){.tty = tty};
  atomic_init(&console->input_locked, false);
  object_init(&console->object, OBJECT_CONSOLE, destroy_console);
  return console;
}

bool console_write(struct console_object *console, const char *bytes, size_t size)
{
  bool locked = log_begin();
  if (!locked) {
    return false;
  }
  if (!console->tty->initialized) {
    log_end(locked);
    return false;
  }

  for (size_t i = 0; i < size; ++i) {
    tty_put_char(console->tty, bytes[i]);
  }
  log_end(locked);
  return true;
}

static struct syscall_result write_console(struct console_object *console,
    const struct console_write_request *payload,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct console_write_request request = *payload;
  struct console_write_reply reply;
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      !user_buffer_check(request.address, request.length, USER_BUFFER_READ)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  /* Capture the payload before output and before writing a possibly aliased
   * reply. Keep each TTY rendering batch small and report partial progress. */
  char bytes[CONSOLE_WRITE_CHUNK];
  size_t count = request.length;
  if (count > sizeof(bytes)) {
    count = sizeof(bytes);
  }
  if (!copy_from_user(bytes, request.address, count)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (count && !console_write(console, bytes, count)) {
    return (struct syscall_result){CALL_UNAVAILABLE, 0};
  }

  reply.written = count;
  /* IF=0, private stable mappings: the checked reply cannot become invalid
   * after the console side effect. A failure here is a kernel invariant bug. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

/* IF=0; input lock -> scheduler queues. Never sleep or access user memory
 * while locked. Output uses its existing independent rendering lock. */
static void lock_input(struct console_object *console)
{
  while (atomic_exchange_explicit(&console->input_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_input(struct console_object *console)
{
  atomic_store_explicit(&console->input_locked, false, memory_order_release);
}

void console_discard_input(struct console_object *console)
{
  lock_input(console);
  console->input_head = console->input_count = 0;
  console->input_lost = false;
  unlock_input(console);
}

static void wake_input_reader(struct console_object *console)
{
  struct task_wait *wait = console->input_wait;
  console->input_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void lose_input(struct console_object *console)
{
  console->input_head = 0;
  console->input_count = 0;
  console->input_lost = true;
  wake_input_reader(console);
}

void console_input_lost(struct console_object *console)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  lock_input(console);
  lose_input(console);
  unlock_input(console);
  cpu_restore_interrupts(flags);
}

void console_input(struct console_object *console, const char *bytes, size_t size)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  lock_input(console);
  if (!console->input_lost) {
    if (size > CONSOLE_INPUT_CAPACITY - console->input_count) {
      lose_input(console);
    } else {
      for (size_t i = 0; i < size; ++i) {
        size_t tail = (console->input_head + console->input_count) % CONSOLE_INPUT_CAPACITY;
        console->input[tail] = bytes[i];
        ++console->input_count;
      }
      if (size) {
        wake_input_reader(console);
      }
    }
  }
  unlock_input(console);
  cpu_restore_interrupts(flags);
}

static void end_read(struct console_object *console);

static enum call_status begin_read(struct console_object *console, bool timed, uint64_t deadline)
{
  lock_input(console);
  if (task_stop_requested()) {
    unlock_input(console);
    return CALL_ENDPOINT_CLOSED;
  }
  if (!console->reader_active) {
    console->reader_active = true;
    unlock_input(console);
    return CALL_OK;
  }

  if (timed && task_deadline_expired(deadline)) {
    unlock_input(console);
    return CALL_TIMED_OUT;
  }

  struct task_wait_link *reader = task_wait_link_prepare();
  struct task_wait *wait = reader->wait;
  if (console->last_reader) {
    console->last_reader->next = reader;
  } else {
    console->first_reader = reader;
  }
  console->last_reader = reader;
  unlock_input(console);
  bool resumed = timed ? task_wait_sleep_until_interruptible(wait, deadline) :
      task_wait_sleep_interruptible(wait);
  lock_input(console);
  bool acquired = reader->wait == NULL;
  if (!acquired) {
    /* Timeout or stop can race the previous reader's handoff. Remove a record
     * still queued; end_read clears wait when it grants ownership. */
    struct task_wait_link **link = &console->first_reader;
    struct task_wait_link *previous = NULL;
    while (*link != reader) {
      KASSERT(*link);
      previous = *link;
      link = &(*link)->next;
    }
    *link = reader->next;
    if (console->last_reader == reader) {
      console->last_reader = previous;
    }
    reader->next = NULL;
    reader->wait = NULL;
  }
  bool stopped = !resumed || task_stop_requested();
  unlock_input(console);
  if (stopped) {
    if (acquired) {
      end_read(console);
    }
    return CALL_ENDPOINT_CLOSED;
  }
  /* Read ownership is handed directly to us, so newcomers cannot overtake. */
  return acquired ? CALL_OK : CALL_TIMED_OUT;
}

static void end_read(struct console_object *console)
{
  lock_input(console);
  struct task_wait_link *reader = console->first_reader;
  if (reader) {
    console->first_reader = reader->next;
    if (!console->first_reader) {
      console->last_reader = NULL;
    }
    struct task_wait *wait = reader->wait;
    reader->next = NULL;
    reader->wait = NULL;
    task_wait_wake(wait);
  } else {
    console->reader_active = false;
  }
  unlock_input(console);
}

static struct syscall_result read_console(struct console_object *console,
    const struct console_read_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct console_read_reply reply = {0};
  bool timed = request->timeout_ms != CONSOLE_WAIT_FOREVER;
  if (timed && request->timeout_ms > UINT32_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t deadline = timed ? task_deadline_after_ms(request->timeout_ms) : 0;
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      !user_buffer_check(request->address, request->capacity, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request->capacity) {
    if (!keyboard_available()) {
      return (struct syscall_result){CALL_UNAVAILABLE, 0};
    }
    enum call_status status = begin_read(console, timed, deadline);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
    lock_input(console);
    while (!console->input_count && !console->input_lost) {
      if (timed && task_deadline_expired(deadline)) {
        unlock_input(console);
        end_read(console);
        return (struct syscall_result){CALL_TIMED_OUT, 0};
      }
      struct task_wait *wait = task_wait_prepare();
      console->input_wait = wait;
      unlock_input(console);
      bool resumed = timed ? task_wait_sleep_until_interruptible(wait, deadline) :
          task_wait_sleep_interruptible(wait);
      lock_input(console);
      /* Timeout and stop wake without taking this lock. Detach before we can
       * reuse its wait record; input may already have detached it instead. */
      if (console->input_wait == wait) {
        console->input_wait = NULL;
      }
      if (!resumed || task_stop_requested()) {
        unlock_input(console);
        end_read(console);
        return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
      }
    }
    if (console->input_lost) {
      console->input_lost = false;
      unlock_input(console);
      end_read(console);
      return (struct syscall_result){CALL_INPUT_LOST, 0};
    }

    char bytes[CONSOLE_READ_CHUNK];
    size_t count = console->input_count;
    if (count > request->capacity) {
      count = request->capacity;
    }
    if (count > sizeof(bytes)) {
      count = sizeof(bytes);
    }
    for (size_t i = 0; i < count; ++i) {
      bytes[i] = console->input[console->input_head];
      console->input_head = (console->input_head + 1) % CONSOLE_INPUT_CAPACITY;
    }
    console->input_count -= count;
    unlock_input(console);
    KASSERT(copy_to_user(request->address, bytes, count));
    reply.read = count;
    end_read(console);
  }

  /* The captured request and stable private mappings also permit overlapping
   * data/reply destinations; the reply is written last. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result set_tab_width(struct console_object *console,
    const struct console_tab_width_request *request)
{
  if (request->columns < CONSOLE_TAB_WIDTH_MIN || request->columns > CONSOLE_TAB_WIDTH_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  bool locked = log_begin();
  if (!locked) {
    return (struct syscall_result){CALL_UNAVAILABLE, 0};
  }
  bool initialized = console->tty->initialized;
  if (initialized) {
    console->tty->tab_width = request->columns;
  }
  log_end(locked);
  return (struct syscall_result){initialized ? CALL_OK : CALL_UNAVAILABLE, 0};
}

struct syscall_result console_call(struct console_object *console, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case CONSOLE_WRITE:
  case CONSOLE_FRESH_LINE:
  case CONSOLE_SET_TAB_WIDTH:
    required = CONSOLE_RIGHT_WRITE;
    break;
  case CONSOLE_READ:
    required = CONSOLE_RIGHT_READ;
    break;
  case CONSOLE_SIZE:
    required = CONSOLE_RIGHTS;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
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
  if (operation == CONSOLE_WRITE) {
    return write_console(console, &request.write, reply_address, reply_capacity);
  }
  if (operation == CONSOLE_READ) {
    return read_console(console, &request.read, reply_address, reply_capacity);
  }
  if (operation == CONSOLE_SET_TAB_WIDTH) {
    return set_tab_width(console, &request.tab_width);
  }

  if (operation == CONSOLE_FRESH_LINE) {
    bool locked = log_begin();
    if (!locked) {
      return (struct syscall_result){CALL_UNAVAILABLE, 0};
    }
    bool initialized = console->tty->initialized;
    if (initialized) {
      tty_fresh_line(console->tty);
    }
    log_end(locked);
    return (struct syscall_result){initialized ? CALL_OK : CALL_UNAVAILABLE, 0};
  }

  struct console_size_reply reply = {
    .columns = console->tty->width,
    .rows = console->tty->height,
  };
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
