#include <kernel/object/mount.h>
#include <kernel/object/echo.h>
#include <kernel/object/net_config.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/clock.h>
#include <abi/message.h>
#include <kernel/object/display.h>
#include <kernel/object/file.h>
#include <kernel/object/process.h>
#include <kernel/object/launcher.h>
#include <kernel/object/memory.h>
#include <kernel/object/directory.h>
#include <kernel/object/capability.h>
#include <kernel/object/console.h>
#include <kernel/object/endpoint.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/user.h>
#include <kernel/user_memory.h>

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

static struct syscall_result copy_handle(handle_t source, uint64_t rights,
    uint64_t flags, uintptr_t destination)
{
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  if (flags & ~HANDLE_COPY_SAME_RIGHTS ||
      ((flags & HANDLE_COPY_SAME_RIGHTS) && rights)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(destination, sizeof(handle_t), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct kernel_object *object;
  uint64_t granted;
  enum capability_result result = capability_resolve(&process->capabilities,
      source, rights, &object, &granted);
  if (result != CAP_OK) {
    KASSERT(result == CAP_BAD_HANDLE || result == CAP_DENIED);
    return (struct syscall_result){result == CAP_BAD_HANDLE ? CALL_BAD_HANDLE : CALL_DENIED, 0};
  }
  if (flags & HANDLE_COPY_SAME_RIGHTS) {
    rights = granted;
  }

  /* The source slot keeps the object alive across a BSP table-growth loan.
   * Keep no entry pointer: growth replaces the table's storage. */
  handle_t handle;
  for (;;) {
    result = capability_insert(&process->capabilities, object, rights, &handle);
    if (result != CAP_FULL) {
      break;
    }
    result = task_grow_capabilities();
    if (result != CAP_OK) {
      break;
    }
  }
  if (result != CAP_OK) {
    KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT);
    return (struct syscall_result){result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT, 0};
  }
  /* Private mappings stay stable while the sole user task is blocked. */
  KASSERT(copy_to_user(destination, &handle, sizeof(handle)));
  return (struct syscall_result){CALL_OK, sizeof(handle)};
}

static struct syscall_result call_object(handle_t handle,
    uintptr_t message_address, size_t message_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }

  struct kernel_object *object;
  uint64_t rights;
  enum capability_result lookup = capability_resolve(&process->capabilities,
      handle, 0, &object, &rights);
  if (lookup == CAP_BAD_HANDLE) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  KASSERT(lookup == CAP_OK);

  struct message_header header;
  if (message_size < sizeof(header)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&header, message_address, sizeof(header))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  /* Reading the header checked this addition. The selected handler captures
   * its fixed payload before any side effect; private mappings stay stable. */
  uintptr_t request_address = message_address + sizeof(header);
  size_t request_size = message_size - sizeof(header);
  switch (object->type) {
  case OBJECT_CONSOLE:
    if (header.protocol != PROTOCOL_CONSOLE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return console_call((struct console_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_FILE:
    if (header.protocol != PROTOCOL_FILE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return file_call((struct file_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_DIRECTORY:
    if (header.protocol != PROTOCOL_DIRECTORY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return directory_call((struct directory_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_KEYBOARD:
    if (header.protocol != PROTOCOL_KEYBOARD) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return keyboard_call((struct keyboard_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_MOUNT:
    if (header.protocol != PROTOCOL_MOUNT) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return mount_call(rights, header.operation, request_size, reply_address, reply_capacity);
  case OBJECT_NET_CONFIG:
    if (header.protocol != PROTOCOL_NET_CONFIG) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return net_config_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_ECHO:
    if (header.protocol != PROTOCOL_ECHO) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return echo_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_CLOCK:
    if (header.protocol != PROTOCOL_CLOCK) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return clock_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_DISPLAY:
    if (header.protocol != PROTOCOL_DISPLAY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return display_call((struct display_object *)object, rights, header.operation,
        request_size, reply_address, reply_capacity);
  case OBJECT_MEMORY:
    if (header.protocol != PROTOCOL_MEMORY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return memory_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_PROCESS_CONTROL:
    if (header.protocol != PROTOCOL_PROCESS) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return process_control_call((struct process_control *)object, rights,
        header.operation, request_size, reply_address, reply_capacity);
  case OBJECT_LAUNCHER:
    if (header.protocol != PROTOCOL_LAUNCHER) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return launcher_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_ENDPOINT:
    if (header.protocol != PROTOCOL_ENDPOINT) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return endpoint_call((struct endpoint *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
}

struct syscall_result syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6)
{
  (void)arg6;
  switch (number) {
  case SYSCALL_CALL:
    return call_object(arg1, arg2, arg3, arg4, arg5);
  case SYSCALL_CLOSE:
    return close_handle(arg1);
  case SYSCALL_COPY:
    return copy_handle(arg1, arg2, arg3, arg4);
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
