#include <abi/console.h>
#include <kernel/capability.h>
#include <kernel/console.h>
#include <kernel/fb/tty.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/user.h>
#include <kernel/user_memory.h>

#define CONSOLE_WRITE_CHUNK 256

static struct syscall_result close_handle(handle_t handle)
{
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }

  enum capability_result result = capability_close(&process->capabilities, handle);
  if (result == CAP_BAD_HANDLE) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  KASSERT(result == CAP_OK);
  return (struct syscall_result){CALL_OK, 0};
}

static struct syscall_result call_object(handle_t handle, uint64_t operation,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }

  struct kernel_object *object;
  enum capability_result lookup = capability_resolve(&process->capabilities,
      handle, CAP_WRITE, &object);
  if (lookup == CAP_BAD_HANDLE) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  if (lookup == CAP_DENIED) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  KASSERT(lookup == CAP_OK);
  if (object->type != OBJECT_CONSOLE || operation != CONSOLE_WRITE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
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
  if (count && !console_write((struct console_object *)object, bytes, count)) {
    return (struct syscall_result){CALL_UNAVAILABLE, 0};
  }

  reply.written = count;
  /* IF=0, private stable mappings: the checked reply cannot become invalid
   * after the console side effect. A failure here is a kernel invariant bug. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6)
{
  switch (number) {
  case SYSCALL_CALL:
    return call_object(arg1, arg2, arg3, arg4, arg5, arg6);
  case SYSCALL_CLOSE:
    return close_handle(arg1);
  case SYSCALL_PUTCHAR: {
    bool locked = log_begin();
    if (locked && get_tty()->initialized) {
      tty_put_char(get_tty(), (char)arg1);
    }
    log_end(locked);
    return (struct syscall_result){0, arg3};
  }
  case SYSCALL_LOG_PUTCHAR: {
    bool locked = log_begin();
    log_putc((char)arg1);
    log_end(locked);
    return (struct syscall_result){0, arg3};
  }
  case SYSCALL_EXIT:
    user_exit((int32_t)(uint32_t)arg1);
  default:
    return (struct syscall_result){UINT64_MAX, arg3};
  }
}
