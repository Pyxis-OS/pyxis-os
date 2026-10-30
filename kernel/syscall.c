#include <kernel/object/wifi.h>
#include <kernel/object/wifi_connection.h>
#include <kernel/object/wifi_auth.h>
#include <kernel/object/screenshot_saver.h>
#include <kernel/object/clipboard.h>
#include <kernel/completion.h>
#include <kernel/object/environment.h>
#include <kernel/object/command_search.h>
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
#include <kernel/object/gamepad.h>
#include <kernel/object/pointer.h>
#include <kernel/object/audio.h>
#include <kernel/object/bluetooth_hci.h>
#include <kernel/object/space.h>
#include <kernel/object/profile.h>
#include <kernel/object/pipe.h>
#include <kernel/object/output.h>
#include <kernel/object/retention.h>
#include <kernel/object/clock.h>
#include <kernel/object/system_info.h>
#include <kernel/object/process_view.h>
#include <kernel/object/input_policy.h>
#include <kernel/object/log.h>
#include <abi/message.h>
#include <kernel/object/display.h>
#include <kernel/object/file.h>
#include <kernel/object/process.h>
#include <kernel/object/launcher.h>
#include <kernel/object/memory.h>
#include <kernel/object/jit.h>
#include <kernel/object/thread.h>
#include <kernel/object/directory.h>
#include <kernel/object/capability.h>
#include <kernel/object/console.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/describe.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/object/power.h>
#include <kernel/object/uefi.h>
#include <abi/uefi.h>
#include <kernel/object/screen_capture.h>
#include <kernel/syscall.h>
#include <kernel/fs/fat32.h>
#include <kernel/task.h>
#include <kernel/user.h>
#include <kernel/user_memory.h>
#include <kernel/user/wait.h>
#include <kernel/user/readiness_set.h>

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

static struct syscall_result handle_info(handle_t handle, uintptr_t destination,
    uint64_t capacity)
{
  if (capacity < sizeof(struct handle_info)) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }

  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }

  struct capability_reference reference;
  enum capability_result result = capability_acquire(&process->capabilities,
      handle, 0, 0, &reference);
  if (result != CAP_OK) {
    KASSERT(result == CAP_BAD_HANDLE || result == CAP_LIMIT || result == CAP_DENIED);
    return (struct syscall_result){result == CAP_LIMIT ? CALL_LIMIT :
      result == CAP_DENIED ? CALL_DENIED : CALL_BAD_HANDLE, 0};
  }
  struct handle_info info = {
    .protocol = object_protocol(reference.object),
    .kind = object_effective_type(reference.object) == OBJECT_ENDPOINT_EXPORT ? HANDLE_KIND_EXPORTED : HANDLE_KIND_NATIVE,
    .rights = reference.rights,
    .transport = reference.transport,
    .restrictions = reference.restrictions,
  };
  capability_release(&reference);
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

  struct capability_grant grant;
  enum capability_result result = flags & HANDLE_COPY_SAME_RIGHTS ?
      capability_grant_acquire_same(&process->capabilities, source, &grant) :
      capability_grant_acquire(&process->capabilities, source, rights, transport, &grant);
  if (result != CAP_OK) {
    KASSERT(result == CAP_BAD_HANDLE || result == CAP_DENIED || result == CAP_LIMIT);
    enum call_status status = result == CAP_LIMIT ? CALL_LIMIT :
        result == CAP_BAD_HANDLE ? CALL_BAD_HANDLE : CALL_DENIED;
    return (struct syscall_result){status, 0};
  }
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  result = capability_validate_grants(&process->capabilities, &grant, 1);
  if (result == CAP_OK) {
    result = capability_request_reservation(1, &reservation, &slot);
  }
  if (result != CAP_OK) {
    capability_grant_release(&grant);
    KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT || result == CAP_DENIED);
    enum call_status status = result == CAP_NO_MEMORY ? CALL_NO_MEMORY :
        result == CAP_LIMIT ? CALL_LIMIT : CALL_DENIED;
    return (struct syscall_result){status, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    capability_grant_release(&grant);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  handle_t handle;
  capability_install_reserved(&reservation, &slot, &grant, 1, &handle);
  if (!copy_returned_handles_to_user(destination, &handle, sizeof(handle), &handle, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(handle)};
}

static struct syscall_result dispatch_object(handle_t handle, struct kernel_object *object,
    uint64_t rights, uint64_t transport, uintptr_t message_address, size_t message_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct message_header header;
  if (message_size < sizeof(header)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct user_buffer_lease thread_lease = {0};
  bool thread_service = object->type == OBJECT_THREAD_SERVICE;
  bool copied;
  if (thread_service) {
    if (!user_buffer_lease_acquire(&thread_lease)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    copied = copy_from_user_leased(&thread_lease, &header, message_address, sizeof(header));
  } else {
    copied = copy_from_user(&header, message_address, sizeof(header));
  }
  if (!copied) {
    if (thread_service) {
      user_buffer_lease_release(&thread_lease);
    }
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  /* Reading the header checked this addition. The selected handler captures
   * its fixed payload before any side effect; private mappings stay stable. */
  uintptr_t request_address = message_address + sizeof(header);
  size_t request_size = message_size - sizeof(header);
  if (thread_service) {
    if (header.protocol == PROTOCOL_THREAD_SERVICE &&
        (header.operation == THREAD_ADDRESS_WAIT || header.operation == THREAD_ADDRESS_WAKE)) {
      /* The fixed packet handler consumes the lease, including before a park. */
      return thread_address_packet_call(rights, header.operation,
          request_address, request_size, reply_address, reply_capacity, &thread_lease);
    }
    user_buffer_lease_release(&thread_lease);
  }
  if (header.protocol == PROTOCOL_COMPLETION) {
    return native_completion_call(object, rights, transport, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  }
  switch (object->type) {
  case OBJECT_WIFI_AUTH:
    if (header.protocol != PROTOCOL_WIFI_AUTH) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return wifi_auth_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_WIFI:
    if (header.protocol != PROTOCOL_WIFI) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    if (header.operation == WIFI_CONNECT_START) {
      return wifi_connection_start_call(object, rights, request_address, request_size,
          reply_address, reply_capacity);
    }
    return wifi_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_WIFI_CONNECT:
  case OBJECT_WIFI_LINK:
    if (header.protocol != (object->type == OBJECT_WIFI_CONNECT ?
        PROTOCOL_WIFI_CONNECT : PROTOCOL_WIFI_LINK)) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return wifi_connection_call(object, rights, header.operation, request_size,
        reply_address, reply_capacity);
  case OBJECT_WIFI_SCAN:
    if (header.protocol != PROTOCOL_WIFI_SCAN) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return wifi_scan_call(object, rights, header.operation, request_size,
        reply_address, reply_capacity);
  case OBJECT_FORWARDER_CONTROLLER:
    if (header.protocol != PROTOCOL_FORWARDER) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return forwarder_controller_call(object, rights, header.operation, request_address, request_size);
  case OBJECT_RETENTION_FACTORY:
    if (header.protocol != PROTOCOL_RETENTION_FACTORY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return retention_factory_call(rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_RETENTION_ACCOUNT:
    if (header.protocol != PROTOCOL_RETENTION_ACCOUNT) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return retention_account_call((struct retention_account *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_PIPE_SERVICE:
    if (header.protocol != PROTOCOL_PIPE_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return pipe_service_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_OUTPUT_PRODUCER:
    if (header.protocol != PROTOCOL_FILE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return output_producer_call((struct output_end *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_OUTPUT_COLLECTOR:
    if (header.protocol != PROTOCOL_OUTPUT_COLLECTOR) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return output_collector_call((struct output_end *)object, rights, header.operation,
        request_size, reply_address, reply_capacity);
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
        header.operation, request_size, reply_address, reply_capacity);
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
  case OBJECT_LOG_STREAM:
    if (header.protocol != PROTOCOL_CONSOLE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return log_stream_call(object, rights, header.operation,
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
  case OBJECT_UEFI:
    if (header.protocol != PROTOCOL_UEFI) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return uefi_call(rights, header.operation, request_address, request_size,
        reply_capacity);
  case OBJECT_SCREENSHOT_SAVER:
    if (header.protocol != PROTOCOL_SCREENSHOT_SAVER) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return screenshot_saver_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_SCREEN_CAPTURE:
    if (header.protocol != PROTOCOL_SCREEN_CAPTURE) {
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    return screen_capture_call(rights, header.operation, request_size,
        reply_address, reply_capacity);
  case OBJECT_BLUETOOTH_HCI:
    if (header.protocol != PROTOCOL_BLUETOOTH_HCI) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return bluetooth_hci_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_KEYBOARD:
    if (header.protocol != PROTOCOL_KEYBOARD) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return keyboard_call((struct keyboard_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_GAMEPAD:
    if (header.protocol != PROTOCOL_GAMEPAD) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return gamepad_call((struct gamepad_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_POINTER:
  case OBJECT_TERMINAL_POINTER:
    if (header.protocol != (object->type == OBJECT_POINTER ?
        PROTOCOL_POINTER : PROTOCOL_TERMINAL_POINTER)) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return pointer_call((struct pointer_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_CLIPBOARD:
    if (header.protocol != PROTOCOL_CLIPBOARD) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return clipboard_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_AUDIO:
    if (header.protocol != PROTOCOL_AUDIO) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return audio_call((struct audio_object *)object, rights, header.operation,
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
  case OBJECT_READINESS_SET:
    if (header.protocol != PROTOCOL_READINESS_SET) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return readiness_set_call(object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_TCP_SERVICE:
    if (header.protocol != PROTOCOL_TCP_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return tcp_service_call(object, rights, header.operation, request_address, request_size,
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
    return udp_service_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_UDP:
    if (header.protocol != PROTOCOL_UDP) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return udp_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_NET_REGISTRY:
  case OBJECT_NET_POLICY:
  case OBJECT_NET_INTERFACE:
    if (header.protocol != object_protocol(object)) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return net_control_call((struct net_control_object *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_ECHO:
    if (header.protocol != PROTOCOL_ECHO) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return echo_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_LOG:
    if (header.protocol != PROTOCOL_LOG) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return log_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_INPUT_POLICY:
    if (header.protocol != PROTOCOL_INPUT_POLICY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return input_policy_call(rights, header.operation, request_address, request_size);
  case OBJECT_SYSTEM_INFO:
    if (header.protocol != PROTOCOL_SYSTEM_INFO) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return system_info_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_PROCESS_VIEW:
    if (header.protocol != PROTOCOL_PROCESS_VIEW) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return process_view_call(rights, header.operation, request_size,
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
  case OBJECT_JIT:
    if (header.protocol != PROTOCOL_JIT) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return jit_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_MEMORY:
    if (header.protocol != PROTOCOL_MEMORY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return memory_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_SHARED_REGION:
    if (header.protocol != PROTOCOL_SHARED_MEMORY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return shared_memory_call((struct shared_region *)object, rights, header.operation,
        request_address, request_size, reply_address, reply_capacity);
  case OBJECT_THREAD_SERVICE:
    if (header.protocol != PROTOCOL_THREAD_SERVICE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return thread_service_call(rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_THREAD_COMPLETION:
    if (header.protocol != PROTOCOL_THREAD_COMPLETION) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return thread_control_call((struct thread_control *)object, rights, header.operation,
        request_size, reply_address, reply_capacity);
  case OBJECT_PROCESS_CONTROL:
    if (header.protocol != PROTOCOL_PROCESS) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return process_control_call((struct process_control *)object, rights,
        header.operation, request_size, reply_address, reply_capacity);
  case OBJECT_PROCESS_INTERRUPT:
    if (header.protocol != PROTOCOL_PROCESS_INTERRUPT) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return process_interrupt_call(object, rights, header.operation, request_size);
  case OBJECT_LAUNCHER:
    if (header.protocol != PROTOCOL_LAUNCHER) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return launcher_call(object, rights, header.operation, request_address, request_size,
        reply_address, reply_capacity);
  case OBJECT_COMMAND_LIST:
    if (header.protocol != PROTOCOL_COMMAND_LIST) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return command_list_call(object, rights, header.operation, request_address,
        request_size, reply_address, reply_capacity);
  case OBJECT_ENVIRONMENT:
  case OBJECT_ENVIRONMENT_SNAPSHOT:
    if (header.protocol != (object->type == OBJECT_ENVIRONMENT ?
        PROTOCOL_ENVIRONMENT : PROTOCOL_ENVIRONMENT_SNAPSHOT) &&
        !(object->type == OBJECT_ENVIRONMENT_SNAPSHOT &&
        header.protocol == PROTOCOL_ENVIRONMENT &&
        (header.operation == ENVIRONMENT_LOOKUP || header.operation == ENVIRONMENT_LOOKUP_FILE ||
        header.operation == ENVIRONMENT_INFO))) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    return environment_call(object, rights, header.operation, request_address,
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

static struct syscall_result call_object(handle_t handle,
    uintptr_t message_address, size_t message_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  struct capability_reference reference;
  enum capability_result lookup = capability_acquire(&process->capabilities,
      handle, 0, 0, &reference);
  if (lookup != CAP_OK) {
    KASSERT(lookup == CAP_BAD_HANDLE || lookup == CAP_LIMIT || lookup == CAP_DENIED);
    return (struct syscall_result){lookup == CAP_LIMIT ? CALL_LIMIT :
      lookup == CAP_DENIED ? CALL_DENIED : CALL_BAD_HANDLE, 0};
  }
  lookup = capability_reference_resolve(&reference);
  if (lookup != CAP_OK) {
    fat32_note_call_failure(reference.object);
    capability_release(&reference);
    enum call_status status = lookup == CAP_LIMIT ? CALL_LIMIT :
        lookup == CAP_NOT_FOUND ? CALL_NOT_FOUND : CALL_DENIED;
    return (struct syscall_result){status, 0};
  }
  struct syscall_result result = dispatch_object(handle, reference.object,
      reference.rights, reference.transport, message_address, message_size,
      reply_address, reply_capacity);
  if (result.status == CALL_OK && !task_operation_start()) {
    result = (struct syscall_result){CALL_DENIED, 0};
  }
  if (result.status != CALL_OK || task_operation_denied()) {
    fat32_note_call_failure(reference.object);
  }
  capability_release(&reference);
  if (task_operation_denied()) {
    result = (struct syscall_result){CALL_DENIED, 0};
  }
  return result;
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
    return handle_info(arg1, arg2, arg3);
  case SYSCALL_COPY:
    return copy_handle(arg1, arg2, arg3, arg4, arg5);
  case SYSCALL_WAIT_MANY:
    return user_wait_many(arg1, arg2, arg3, arg4);
  case SYSCALL_READINESS_SET_CREATE:
    return readiness_set_create_call(arg1, arg2);
  case SYSCALL_PROCESS_INTERRUPT_CREATE:
    return process_interrupt_create_call(arg1, arg2);
  case SYSCALL_FORWARDER_CREATE:
    return forwarder_create_call(arg1, arg2, arg3);
  case SYSCALL_DESCRIBE:
    return describe_call(arg1, arg2, arg3, arg4);
  case SYSCALL_SPACE_PROCESSES:
    return space_processes_call(arg1, arg2);
  case SYSCALL_LOG_PUTCHAR:
    log_putchar(process_current()->space, (char)arg1);
    return (struct syscall_result){0, arg3};
  case SYSCALL_START_FAILED:
    if (arg1 > INT32_MAX || !process_start_failure_valid((int64_t)arg1)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    user_start_failed((uint32_t)arg1);
  case SYSCALL_EXIT:
    user_exit((int32_t)(uint32_t)arg1);
  default:
    return (struct syscall_result){(uint64_t)CALL_UNKNOWN_SYSTEM_CALL, arg3};
  }
}

struct syscall_result syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
    uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
  task_syscall_enter();
  struct syscall_result result = dispatch(number, arg1, arg2, arg3, arg4, arg5, arg6);
  task_forwarder_finish();
  task_syscall_leave();
  return result;
}
