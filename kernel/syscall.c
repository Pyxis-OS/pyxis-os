#include <kernel/object/namespace.h>
#include <kernel/object/terminal.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/udp.h>
#include <kernel/object/tcp.h>
#include <kernel/object/random.h>
#include <kernel/object/mount.h>
#include <kernel/object/disk.h>
#include <kernel/object/echo.h>
#include <kernel/object/net_config.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/pointer.h>
#include <kernel/object/space.h>
#include <kernel/object/profile.h>
#include <kernel/object/pipe.h>
#include <kernel/object/clock.h>
#include <kernel/object/system_info.h>
#include <kernel/object/log.h>
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
#include <kernel/object/power.h>
#include <kernel/object/screen_capture.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/user.h>
#include <kernel/user_memory.h>
#include <kernel/user/wait.h>

static struct syscall_result close_handle(handle_t handle)
{
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }

  struct kernel_object *object;
  if (capability_resolve(&process->capabilities, handle, 0, 0,
        &object, NULL, NULL) != CAP_OK) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  endpoint_handle_close(object);
  enum capability_result result = capability_close(&process->capabilities, handle);
  if (result == CAP_BAD_HANDLE) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  KASSERT(result == CAP_OK);
  return (struct syscall_result){CALL_OK, 0};
}

static struct syscall_result handle_info(handle_t handle, uintptr_t destination)
{
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }

  struct kernel_object *object;
  struct handle_info info;
  enum capability_result result = capability_resolve(&process->capabilities,
      handle, 0, 0, &object, &info.rights, &info.transport);
  if (result == CAP_BAD_HANDLE) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  KASSERT(result == CAP_OK);
  info.protocol = object_protocol(object);
  info.kind = object->type == OBJECT_ENDPOINT_EXPORT ?
      HANDLE_KIND_EXPORTED : HANDLE_KIND_NATIVE;
  if (!copy_to_user(destination, &info, sizeof(info))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(info)};
}

static struct syscall_result copy_handle(handle_t source, uint64_t rights,
    uint64_t transport, uint64_t flags, uintptr_t destination)
{
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  if (flags & ~HANDLE_COPY_SAME_RIGHTS ||
      ((flags & HANDLE_COPY_SAME_RIGHTS) && (rights || transport))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(destination, sizeof(handle_t), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct kernel_object *object;
  uint64_t granted;
  uint64_t granted_transport;
  enum capability_result result = capability_resolve(&process->capabilities,
      source, rights, transport, &object, &granted, &granted_transport);
  if (result != CAP_OK) {
    KASSERT(result == CAP_BAD_HANDLE || result == CAP_DENIED);
    return (struct syscall_result){result == CAP_BAD_HANDLE ? CALL_BAD_HANDLE : CALL_DENIED, 0};
  }
  if (object->type == OBJECT_ENDPOINT_RECEIPT || object->type == OBJECT_ENDPOINT_RECEIVER) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (flags & HANDLE_COPY_SAME_RIGHTS) {
    rights = granted;
    transport = granted_transport;
  }

  /* The source slot keeps the object alive across a BSP table-growth loan.
   * Keep no entry pointer: growth replaces the table's storage. */
  handle_t handle;
  for (;;) {
    result = capability_insert(&process->capabilities, object, rights,
        transport, &handle);
    if (result != CAP_FULL) {
      break;
    }
    result = capability_request_growth();
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
  uint64_t transport;
  enum capability_result lookup = capability_resolve(&process->capabilities,
      handle, 0, 0, &object, &rights, &transport);
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
  case OBJECT_PIPE_SERVICE:
    if (header.protocol != PROTOCOL_PIPE_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return pipe_service_call(rights, header.operation, request_size,
        reply_address, reply_capacity);
  case OBJECT_PIPE:
    if (header.protocol != PROTOCOL_PIPE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return pipe_call((struct pipe_end *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_TERMINAL_SERVICE:
    if (header.protocol != PROTOCOL_TERMINAL_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return terminal_service_call(rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_EXECUTION_GROUP:
    if (header.protocol != PROTOCOL_EXECUTION_GROUP) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return execution_group_call((struct execution_group *)object, rights,
        header.operation, request_size);
  case OBJECT_TERMINAL_ATTACHMENT:
    if (header.protocol != PROTOCOL_TERMINAL_ATTACHMENT) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return terminal_attachment_call(object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_TERMINAL_EVENTS:
    if (header.protocol != PROTOCOL_TERMINAL_EVENTS) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return terminal_events_call(object, rights, header.operation,
        request_address, request_size);
  case OBJECT_TERMINAL_INPUT:
  case OBJECT_TERMINAL_OUTPUT:
    if (header.protocol != PROTOCOL_CONSOLE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return terminal_application_call(object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
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
  case OBJECT_PROFILE:
    if (header.protocol != PROTOCOL_PROFILE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return profile_call(rights, header.operation, request_size, reply_address, reply_capacity);
  case OBJECT_SPACE:
    if (header.protocol != PROTOCOL_SPACE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return space_control_call(object, rights, header.operation,
        request_address, request_size);
  case OBJECT_SPACE_FACTORY:
    if (header.protocol != PROTOCOL_SPACE_FACTORY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return space_factory_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_POWER:
    if (header.protocol != PROTOCOL_POWER) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return power_call(rights, header.operation, request_size, reply_capacity);
  case OBJECT_SCREEN_CAPTURE:
    if (header.protocol != PROTOCOL_SCREEN_CAPTURE) {
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    return screen_capture_call(rights, header.operation, request_size,
        reply_address, reply_capacity);
  case OBJECT_KEYBOARD:
    if (header.protocol != PROTOCOL_KEYBOARD) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return keyboard_call((struct keyboard_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_POINTER:
  case OBJECT_TERMINAL_POINTER:
    if (header.protocol != (object->type == OBJECT_POINTER ?
        PROTOCOL_POINTER : PROTOCOL_TERMINAL_POINTER)) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return pointer_call((struct pointer_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_DISKS:
    if (header.protocol != PROTOCOL_DISKS) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return disks_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_DISK:
    if (header.protocol != PROTOCOL_DISK) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return disk_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_MOUNT:
    if (header.protocol != PROTOCOL_MOUNT) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return mount_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_RANDOM:
    if (header.protocol != PROTOCOL_RANDOM) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return random_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_TCP_SERVICE:
    if (header.protocol != PROTOCOL_TCP_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return tcp_service_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_TCP:
    if (header.protocol != PROTOCOL_TCP) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return tcp_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_TCP_LISTENER:
    if (header.protocol != PROTOCOL_TCP_LISTENER) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return tcp_listener_call(object, rights, header.operation, request_address,
        request_size, reply_address, reply_capacity);
  case OBJECT_UDP_SERVICE:
    if (header.protocol != PROTOCOL_UDP_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return udp_service_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_UDP:
    if (header.protocol != PROTOCOL_UDP) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return udp_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
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
  case OBJECT_LOG:
    if (header.protocol != PROTOCOL_LOG) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return log_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_SYSTEM_INFO:
    if (header.protocol != PROTOCOL_SYSTEM_INFO) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return system_info_call(rights, header.operation, request_address, request_size,
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
        request_address, request_size, reply_address, reply_capacity);
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
    return launcher_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_NAMESPACE_SERVICE:
    if (header.protocol != PROTOCOL_NAMESPACE_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return namespace_service_call(rights, header.operation, request_size,
        reply_address, reply_capacity);
  case OBJECT_NAMESPACE:
    if (header.protocol != PROTOCOL_NAMESPACE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return namespace_call(object, rights, header.operation, request_address,
        request_size, reply_address, reply_capacity);
  case OBJECT_ENDPOINT_SERVICE:
    if (header.protocol != PROTOCOL_ENDPOINT_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return endpoint_service_call(rights, header.operation, request_address,
        request_size, reply_address, reply_capacity);
  case OBJECT_ENDPOINT_RECEIVER:
  case OBJECT_ENDPOINT_RECEIPT:
  case OBJECT_ENDPOINT:
  case OBJECT_ENDPOINT_EXPORT:
    if ((object->type == OBJECT_ENDPOINT && header.protocol != PROTOCOL_ENDPOINT) ||
        (object->type == OBJECT_ENDPOINT_EXPORT && header.protocol != PROTOCOL_ENDPOINT) ||
        (object->type == OBJECT_ENDPOINT_RECEIVER && header.protocol != PROTOCOL_ENDPOINT_RECEIVER) ||
        (object->type == OBJECT_ENDPOINT_RECEIPT && header.protocol != PROTOCOL_ENDPOINT_RECEIPT)) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return endpoint_call(object, handle, rights, transport, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
}

static struct syscall_result dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6)
{
  (void)arg6;
  switch (number) {
  case SYSCALL_CALL:
    return call_object(arg1, arg2, arg3, arg4, arg5);
  case SYSCALL_CLOSE:
    return close_handle(arg1);
  case SYSCALL_HANDLE_INFO:
    return handle_info(arg1, arg2);
  case SYSCALL_COPY:
    return copy_handle(arg1, arg2, arg3, arg4, arg5);
  case SYSCALL_WAIT_MANY:
    return user_wait_many(arg1, arg2, arg3, arg4);
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

struct syscall_result syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
    uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
  task_syscall_enter();
  struct syscall_result result = dispatch(number, arg1, arg2, arg3, arg4, arg5, arg6);
  task_syscall_leave();
  return result;
}
