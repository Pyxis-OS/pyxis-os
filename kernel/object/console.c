#include <abi/console.h>
#include <arch/smp.h>
#include <kernel/object/console.h>
#include <kernel/object/capability.h>
#include <kernel/user_memory.h>
#include <kernel/fb/tty.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>

#define CONSOLE_WRITE_CHUNK 256

static void destroy_console(struct kernel_object *object)
{
  /* The reference header is the first member; only the wrapper is owned. */
  kfree((struct console_object *)object);
}

struct console_object *console_create(struct tty *tty)
{
  KASSERT(arch_cpu_index() == 0 && tty && tty->initialized);
  struct console_object *console = kmalloc(sizeof(*console));
  if (!console) {
    return NULL;
  }
  object_init(&console->object, OBJECT_CONSOLE, destroy_console);
  console->tty = tty;
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

struct syscall_result console_call(struct console_object *console, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != CONSOLE_WRITE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & CONSOLE_RIGHT_WRITE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }

  struct console_write_request request;
  struct console_write_reply reply;
  if (request_size != sizeof(request) || reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request)) ||
      !user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
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
