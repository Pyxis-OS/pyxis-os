#include <kernel/object/wifi.h>
#include <kernel/object/wifi_connection.h>
#include <kernel/object/wifi_auth.h>
#include <kernel/object/screenshot_saver.h>
#include <abi/clipboard.h>
#include <abi/environment.h>
#include <abi/terminal.h>
#include <kernel/object/terminal.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/launcher.h>
#include <abi/profile.h>
#include <abi/pipe.h>
#include <kernel/object/pipe.h>
#include <kernel/object/output.h>
#include <kernel/object/retention.h>
#include <abi/space.h>
#include <abi/udp.h>
#include <abi/tcp.h>
#include <abi/random.h>
#include <abi/mount.h>
#include <abi/disk.h>
#include <kernel/object/disk.h>
#include <kernel/object/mount.h>
#include <abi/echo.h>
#include <abi/net_config.h>
#include <abi/keyboard.h>
#include <abi/gamepad.h>
#include <abi/pointer.h>
#include <abi/audio.h>
#include <abi/terminal_pointer.h>
#include <abi/clock.h>
#include <abi/system_info.h>
#include <abi/input_policy.h>
#include <abi/log.h>
#include <abi/display.h>
#include <abi/screen_capture.h>
#include <abi/bluetooth_hci.h>
#include <abi/file.h>
#include <abi/process.h>
#include <abi/thread.h>
#include <abi/launcher.h>
#include <abi/memory.h>
#include <abi/jit.h>
#include <abi/directory.h>
#include <abi/console.h>
#include <abi/endpoint.h>
#include <arch/smp.h>
#include <kernel/object/console.h>
#include <kernel/object/process.h>
#include <abi/power.h>
#include <abi/uefi.h>
#include <kernel/object/object.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/file.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/environment.h>
#include <kernel/object/command_search.h>
#include <kernel/panic.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/task.h>
#include <kernel/net/tcp.h>
#include <kernel/user/readiness_set.h>
#include <stdint.h>

static struct kernel_object *retired_objects;
static atomic_bool retired_locked;

uint64_t object_protocol(const struct kernel_object *object)
{
  switch (object->type) {
  case OBJECT_FORWARDER:
    return forwarder_protocol(object);
  case OBJECT_FORWARDER_CONTROLLER:
    return PROTOCOL_FORWARDER;
  case OBJECT_CLIPBOARD:
    return PROTOCOL_CLIPBOARD;
  case OBJECT_EXECUTION_GROUP:
    return PROTOCOL_EXECUTION_GROUP;
  case OBJECT_TERMINAL_SERVICE:
    return PROTOCOL_TERMINAL_SERVICE;
  case OBJECT_TERMINAL_ATTACHMENT:
    return PROTOCOL_TERMINAL_ATTACHMENT;
  case OBJECT_TERMINAL_EVENTS:
    return PROTOCOL_TERMINAL_EVENTS;
  case OBJECT_TERMINAL_INPUT:
  case OBJECT_TERMINAL_OUTPUT:
  case OBJECT_CONSOLE:
    return PROTOCOL_CONSOLE;
  case OBJECT_FILE:
  case OBJECT_OUTPUT_PRODUCER:
    return PROTOCOL_FILE;
  case OBJECT_OUTPUT_COLLECTOR:
    return PROTOCOL_OUTPUT_COLLECTOR;
  case OBJECT_RETENTION_FACTORY:
    return PROTOCOL_RETENTION_FACTORY;
  case OBJECT_RETENTION_ACCOUNT:
    return PROTOCOL_RETENTION_ACCOUNT;
  case OBJECT_ENDPOINT:
    return PROTOCOL_ENDPOINT;
  case OBJECT_DIRECTORY:
    return PROTOCOL_DIRECTORY;
  case OBJECT_MEMORY:
    return PROTOCOL_MEMORY;
  case OBJECT_JIT:
    return PROTOCOL_JIT;
  case OBJECT_PROCESS_CONTROL:
    return PROTOCOL_PROCESS;
  case OBJECT_PROCESS_INTERRUPT:
    return PROTOCOL_PROCESS_INTERRUPT;
  case OBJECT_THREAD_SERVICE:
    return PROTOCOL_THREAD_SERVICE;
  case OBJECT_THREAD_COMPLETION:
    return PROTOCOL_THREAD_COMPLETION;
  case OBJECT_LAUNCHER:
    return PROTOCOL_LAUNCHER;
  case OBJECT_DISPLAY:
    return PROTOCOL_DISPLAY;
  case OBJECT_WIFI:
    return PROTOCOL_WIFI;
  case OBJECT_WIFI_SCAN:
    return PROTOCOL_WIFI_SCAN;
  case OBJECT_WIFI_CONNECT:
    return PROTOCOL_WIFI_CONNECT;
  case OBJECT_WIFI_LINK:
    return PROTOCOL_WIFI_LINK;
  case OBJECT_WIFI_AUTH:
    return PROTOCOL_WIFI_AUTH;
  case OBJECT_SCREENSHOT_SAVER:
    return PROTOCOL_SCREENSHOT_SAVER;
  case OBJECT_SCREEN_CAPTURE:
    return PROTOCOL_SCREEN_CAPTURE;
  case OBJECT_BLUETOOTH_HCI:
    return PROTOCOL_BLUETOOTH_HCI;
  case OBJECT_INPUT_POLICY:
    return PROTOCOL_INPUT_POLICY;
  case OBJECT_SYSTEM_INFO:
    return PROTOCOL_SYSTEM_INFO;
  case OBJECT_LOG:
    return PROTOCOL_LOG;
  case OBJECT_CLOCK:
    return PROTOCOL_CLOCK;
  case OBJECT_KEYBOARD:
    return PROTOCOL_KEYBOARD;
  case OBJECT_GAMEPAD:
    return PROTOCOL_GAMEPAD;
  case OBJECT_POINTER:
    return PROTOCOL_POINTER;
  case OBJECT_AUDIO:
    return PROTOCOL_AUDIO;
  case OBJECT_TERMINAL_POINTER:
    return PROTOCOL_TERMINAL_POINTER;
  case OBJECT_MOUNT:
    return PROTOCOL_MOUNT;
  case OBJECT_DISKS:
    return PROTOCOL_DISKS;
  case OBJECT_DISK:
    return PROTOCOL_DISK;
  case OBJECT_ECHO:
    return PROTOCOL_ECHO;
  case OBJECT_NET_REGISTRY:
    return PROTOCOL_NET_REGISTRY;
  case OBJECT_NET_POLICY:
    return PROTOCOL_NET_POLICY;
  case OBJECT_NET_INTERFACE:
    return PROTOCOL_NET_INTERFACE;
  case OBJECT_UDP_SERVICE:
    return PROTOCOL_UDP_SERVICE;
  case OBJECT_UDP:
    return PROTOCOL_UDP;
  case OBJECT_RANDOM:
    return PROTOCOL_RANDOM;
  case OBJECT_READINESS_SET:
    return PROTOCOL_READINESS_SET;
  case OBJECT_TCP_SERVICE:
    return PROTOCOL_TCP_SERVICE;
  case OBJECT_TCP:
    return PROTOCOL_TCP;
  case OBJECT_TCP_LISTENER:
    return PROTOCOL_TCP_LISTENER;
  case OBJECT_SPACE:
    return PROTOCOL_SPACE;
  case OBJECT_SPACE_FACTORY:
    return PROTOCOL_SPACE_FACTORY;
  case OBJECT_POWER:
    return PROTOCOL_POWER;
  case OBJECT_UEFI:
    return PROTOCOL_UEFI;
  case OBJECT_PROFILE:
    return PROTOCOL_PROFILE;
  case OBJECT_PIPE_SERVICE:
    return PROTOCOL_PIPE_SERVICE;
  case OBJECT_PIPE:
    return PROTOCOL_PIPE;
  case OBJECT_ENDPOINT_SERVICE:
    return PROTOCOL_ENDPOINT_SERVICE;
  case OBJECT_ENDPOINT_RECEIVER:
    return PROTOCOL_ENDPOINT_RECEIVER;
  case OBJECT_ENDPOINT_RECEIPT:
    return PROTOCOL_ENDPOINT_RECEIPT;
  case OBJECT_ENVIRONMENT:
    return PROTOCOL_ENVIRONMENT;
  case OBJECT_ENVIRONMENT_SNAPSHOT:
    return PROTOCOL_ENVIRONMENT_SNAPSHOT;
  case OBJECT_COMMAND_LIST:
    return PROTOCOL_COMMAND_LIST;
  case OBJECT_ENDPOINT_EXPORT:
    return endpoint_export_protocol(object);
  default:
    return 0;
  }
}

enum object_type object_effective_type(const struct kernel_object *object)
{
  return object->type == OBJECT_FORWARDER ? forwarder_type(object) : object->type;
}

bool object_stream_valid(const struct kernel_object *object, uint64_t protocol,
                         uint64_t transport)
{
  enum object_type type = object_effective_type(object);
  if (type == OBJECT_ENDPOINT_EXPORT) {
    return (protocol == PROTOCOL_FILE || protocol == PROTOCOL_STREAM) &&
        object_protocol(object) == protocol && transport == HANDLE_TRANSPORT_CALL;
  }
  if (transport) {
    return false;
  }
  return (protocol == PROTOCOL_FILE && (type == OBJECT_FILE || type == OBJECT_OUTPUT_PRODUCER)) ||
      (protocol == PROTOCOL_CONSOLE && (type == OBJECT_CONSOLE ||
          type == OBJECT_TERMINAL_INPUT || type == OBJECT_TERMINAL_OUTPUT)) ||
      (protocol == PROTOCOL_PIPE && type == OBJECT_PIPE);
}

/* Armed and passthrough handles carry exactly one right, so their grant
 * accounting never shares a handle with READ or INTERRUPT authority. */
static bool console_hold_rights(uint64_t rights)
{
  return rights == CONSOLE_RIGHT_ARMED || rights == CONSOLE_RIGHT_PASSTHROUGH;
}

bool object_authority_valid(const struct kernel_object *object, uint64_t rights,
                            uint64_t transport)
{
  if (!object) {
    return false;
  }
  if (object->type == OBJECT_FORWARDER || object->type == OBJECT_FORWARDER_CONTROLLER) {
    return forwarder_authority_valid(object, rights, transport);
  }
  if (object->type == OBJECT_ENDPOINT) {
    return !rights && !(transport & ~HANDLE_TRANSPORT_CALL);
  }
  if (object->type == OBJECT_ENDPOINT_RECEIVER) {
    return !(rights & ~ENDPOINT_RECEIVER_RIGHT_CONTROL) &&
        !(transport & ~HANDLE_TRANSPORT_RECEIVE);
  }
  if (object->type == OBJECT_ENDPOINT_EXPORT) {
    return endpoint_export_authority_valid(object, rights, transport);
  }
  if (transport) {
    return false;
  }

  switch (object->type) {
  case OBJECT_CLIPBOARD:
    return !(rights & ~CLIPBOARD_RIGHTS);
  case OBJECT_ENVIRONMENT:
    return !(rights & ~ENVIRONMENT_RIGHTS);
  case OBJECT_ENVIRONMENT_SNAPSHOT:
    return !(rights & ~ENVIRONMENT_RIGHT_READ);
  case OBJECT_COMMAND_LIST:
    return !(rights & ~COMMAND_LIST_RIGHT_READ);
  case OBJECT_OUTPUT_PRODUCER:
    return !(rights & ~FILE_RIGHT_WRITE);
  case OBJECT_OUTPUT_COLLECTOR:
    return !(rights & ~OUTPUT_RIGHT_COLLECT);
  case OBJECT_RETENTION_FACTORY:
    return !(rights & ~RETENTION_FACTORY_RIGHT_CREATE);
  case OBJECT_RETENTION_ACCOUNT:
    return !(rights & ~RETENTION_RIGHT_CAPTURE);
  case OBJECT_PIPE_SERVICE:
    return !(rights & ~PIPE_SERVICE_RIGHT_CREATE);
  case OBJECT_PIPE:
    return !(rights & ~(PIPE_RIGHT_READ | PIPE_RIGHT_WRITE));
  case OBJECT_PROFILE:
    return !(rights & ~(PROFILE_RIGHT_MEMORY | PROFILE_RIGHT_HOST));
  case OBJECT_SPACE:
    return !(rights & ~SPACE_RIGHTS);
  case OBJECT_SPACE_FACTORY:
    return !(rights & ~SPACE_FACTORY_RIGHTS);
  case OBJECT_POWER:
    return !(rights & ~POWER_RIGHTS);
  case OBJECT_UEFI:
    return !(rights & ~UEFI_RIGHTS);
  case OBJECT_WIFI:
    return !(rights & ~(WIFI_RIGHTS | WIFI_RIGHT_SUPPLICANT));
  case OBJECT_WIFI_SCAN:
    return !(rights & ~WIFI_SCAN_RIGHT_COLLECT);
  case OBJECT_WIFI_CONNECT:
    return !(rights & ~WIFI_CONNECT_RIGHT_COLLECT);
  case OBJECT_WIFI_LINK:
    return !(rights & ~(WIFI_LINK_RIGHT_READ | WIFI_LINK_RIGHT_CONTROL));
  case OBJECT_WIFI_AUTH:
    return !(rights & ~(WIFI_AUTH_RIGHT_RUN | WIFI_AUTH_RIGHT_OBSERVE));
  case OBJECT_SCREENSHOT_SAVER:
    return !(rights & ~SCREENSHOT_SAVER_RIGHT_SERVE);
  case OBJECT_SCREEN_CAPTURE:
    return !(rights & ~SCREEN_CAPTURE_RIGHT_CAPTURE);
  case OBJECT_BLUETOOTH_HCI:
    return !(rights & ~BLUETOOTH_HCI_RIGHT_CONTROL);
  case OBJECT_TERMINAL_SERVICE:
    return !(rights & ~TERMINAL_SERVICE_RIGHT_CREATE);
  case OBJECT_TERMINAL_ATTACHMENT:
    return !(rights & ~TERMINAL_RIGHTS);
  case OBJECT_TERMINAL_EVENTS:
    return !(rights & ~TERMINAL_EVENTS_RIGHT_EMIT);
  case OBJECT_TERMINAL_INPUT:
    return console_hold_rights(rights) ||
        !(rights & ~(CONSOLE_RIGHT_READ | CONSOLE_RIGHT_INTERRUPT));
  case OBJECT_TERMINAL_OUTPUT:
    return !(rights & ~CONSOLE_RIGHT_WRITE);
  case OBJECT_CONSOLE:
    return console_hold_rights(rights) ||
        !(rights & ~(CONSOLE_RIGHTS | CONSOLE_RIGHT_INTERRUPT));
  case OBJECT_FILE:
    return !(rights & ~((((const struct file_object *)object)->immutable_snapshot ||
        file_failure_status((const struct file_object *)object) != CALL_OK) ?
        FILE_RIGHT_READ : FILE_RIGHTS));
  case OBJECT_DIRECTORY:
    return !(rights & ~DIRECTORY_RIGHTS);
  case OBJECT_MEMORY:
    return !(rights & ~MEMORY_RIGHT_MANAGE);
  case OBJECT_JIT:
    return !(rights & ~JIT_RIGHT_MANAGE);
  case OBJECT_PROCESS_CONTROL:
    return !(rights & ~PROCESS_RIGHTS);
  case OBJECT_PROCESS_INTERRUPT:
    return !(rights & ~PROCESS_INTERRUPT_RIGHT_RECEIVE);
  case OBJECT_THREAD_SERVICE:
    return !(rights & ~(THREAD_RIGHT_MANAGE | THREAD_RIGHT_SET_FS));
  case OBJECT_THREAD_COMPLETION:
    return !(rights & ~THREAD_COMPLETION_RIGHT_WAIT);
  case OBJECT_KEYBOARD:
    return !(rights & ~KEYBOARD_RIGHT_INPUT);
  case OBJECT_GAMEPAD:
    return !(rights & ~GAMEPAD_RIGHT_INPUT);
  case OBJECT_POINTER:
    return !(rights & ~POINTER_RIGHT_INPUT);
  case OBJECT_AUDIO:
    return !(rights & ~AUDIO_RIGHT_PLAYBACK);
  case OBJECT_TERMINAL_POINTER:
    return !(rights & ~TERMINAL_POINTER_RIGHT_CONTROL);
  case OBJECT_MOUNT:
    return mount_rights_valid((const struct mount_object *)object, rights);
  case OBJECT_DISKS:
    return !(rights & ~DISKS_RIGHTS);
  case OBJECT_DISK:
    return disk_rights_valid((const struct disk_object *)object, rights);
  case OBJECT_RANDOM:
    return !(rights & ~RANDOM_RIGHT_READ);
  case OBJECT_READINESS_SET:
    return !(rights & ~READINESS_SET_RIGHTS);
  case OBJECT_TCP_SERVICE:
    return !(rights & ~TCP_SERVICE_RIGHTS);
  case OBJECT_TCP:
    return !(rights & ~TCP_RIGHTS);
  case OBJECT_TCP_LISTENER:
    return !(rights & ~TCP_LISTENER_RIGHTS);
  case OBJECT_UDP_SERVICE:
    return !(rights & ~(UDP_SERVICE_RIGHT_OPEN | UDP_SERVICE_RIGHT_BROADCAST));
  case OBJECT_UDP:
    return !(rights & ~UDP_RIGHTS);
  case OBJECT_NET_REGISTRY:
    return !(rights & ~NET_REGISTRY_RIGHTS);
  case OBJECT_NET_POLICY:
    return !(rights & ~NET_POLICY_RIGHTS);
  case OBJECT_NET_INTERFACE:
    return !(rights & ~NET_INTERFACE_RIGHTS);
  case OBJECT_ECHO:
    return !(rights & ~ECHO_RIGHT_SEND);
  case OBJECT_INPUT_POLICY:
    return !(rights & ~INPUT_POLICY_RIGHT_INSTALL_ONCE);
  case OBJECT_SYSTEM_INFO:
    return !(rights & ~(SYSTEM_INFO_RIGHT_READ | SYSTEM_INFO_RIGHT_SET_HOSTNAME_ONCE));
  case OBJECT_LOG:
    return !(rights & ~LOG_RIGHT_READ);
  case OBJECT_CLOCK:
    return !(rights & ~CLOCK_RIGHTS);
  case OBJECT_DISPLAY:
    return !(rights & ~DISPLAY_RIGHT_DRAW);
  case OBJECT_LAUNCHER:
    return !(rights & ~LAUNCHER_RIGHT_LAUNCH);
  case OBJECT_EXECUTION_GROUP:
    return !(rights & ~(EXECUTION_GROUP_RIGHT_CONTROL | EXECUTION_GROUP_RIGHT_WAIT));
  case OBJECT_ENDPOINT_SERVICE:
    return !(rights & ~ENDPOINT_SERVICE_RIGHT_CREATE);
  case OBJECT_ENDPOINT_RECEIPT:
    return !(rights & ~(ENDPOINT_RIGHT_REPLY | ENDPOINT_RIGHT_COLLECT));
  default:
    return false;
  }
}

static void lock_retired(void)
{
  while (atomic_exchange_explicit(&retired_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_retired(void)
{
  atomic_store_explicit(&retired_locked, false, memory_order_release);
}

void object_init(struct kernel_object *object, enum object_type type,
                 void (*destroy)(struct kernel_object *))
{
  KASSERT(object && (destroy || type == OBJECT_ENDPOINT_RECEIPT));
  object->type = type;
  atomic_init(&object->references, 1);
  object->retired_next = NULL;
  object->cleanup_group = NULL;
  object->destroy = destroy;
}

bool object_retain(struct kernel_object *object)
{
  size_t count = atomic_load_explicit(&object->references, memory_order_relaxed);
  for (;;) {
    KASSERT(count);
    if (count == SIZE_MAX) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&object->references, &count,
          count + 1, memory_order_relaxed, memory_order_relaxed)) {
      return true;
    }
  }
}

struct execution_group *object_cleanup_enter(struct execution_group *group)
{
  return task_cleanup_set_group(group);
}

void object_cleanup_leave(struct execution_group *previous)
{
  task_cleanup_set_group(previous);
}

struct execution_group *object_cleanup_defer(void)
{
  struct execution_group *group = task_cleanup_group();
  if (group) {
    execution_group_cleanup_begin(group);
  }
  return group;
}

void object_release(struct kernel_object *object)
{
  if (object->type == OBJECT_FORWARDER) {
    if (!forwarder_storage_release(object)) {
      return;
    }
  } else if (object->type == OBJECT_ENDPOINT_EXPORT) {
    if (!endpoint_export_storage_release(object)) {
      return;
    }
  } else {
    size_t previous = atomic_fetch_sub_explicit(&object->references, 1,
                                                memory_order_acq_rel);
    KASSERT(previous);
    if (previous != 1) {
      return;
    }
  }

  if (object->type == OBJECT_ENDPOINT_RECEIPT) {
    endpoint_receipt_release(object);
    return;
  }

  /* Group storage itself must not keep its own completion pending. */
  if (object->type != OBJECT_EXECUTION_GROUP) {
    object->cleanup_group = object_cleanup_defer();
  }

  /* The last owner lends the object's own link to the queue. Even an AP can
   * retire it without allocating a queue node or touching the heap. */
  lock_retired();
  object->retired_next = retired_objects;
  retired_objects = object;
  unlock_retired();
  /* The BSP may destroy object immediately after publication. */
}

bool object_caretaker_retain(struct kernel_object *object)
{
  return object->type == OBJECT_ENDPOINT_EXPORT ?
      endpoint_export_caretaker_retain(object) : object_retain(object);
}

void object_caretaker_release(struct kernel_object *object)
{
  if (object->type == OBJECT_ENDPOINT_EXPORT) {
    endpoint_export_caretaker_release(object);
  } else {
    object_release(object);
  }
}

bool object_grant_retain(struct kernel_object *object, uint64_t rights)
{
  if (!object_retain(object)) {
    return false;
  }
  bool retained;
  if (object->type == OBJECT_FORWARDER || object->type == OBJECT_FORWARDER_CONTROLLER) {
    retained = forwarder_grant_retain(object, rights);
  } else if (object->type == OBJECT_READINESS_SET) {
    retained = readiness_set_grant_retain(object, rights);
  } else if (object->type == OBJECT_PROCESS_INTERRUPT) {
    retained = process_interrupt_grant_retain(object, rights);
  } else if (object->type == OBJECT_TCP || object->type == OBJECT_TCP_LISTENER) {
    retained = tcp_grant_retain(object, rights);
  } else if (object->type == OBJECT_WIFI_CONNECT || object->type == OBJECT_WIFI_LINK) {
    retained = wifi_connection_grant_retain(object, rights);
  } else if (object->type == OBJECT_WIFI_AUTH) {
    retained = wifi_auth_grant_retain(object, rights);
  } else if (object->type == OBJECT_WIFI_SCAN) {
    retained = wifi_scan_grant_retain(object);
  } else if (object->type == OBJECT_ENVIRONMENT_SNAPSHOT) {
    retained = environment_snapshot_grant_retain(object);
  } else if (object->type == OBJECT_COMMAND_LIST) {
    retained = command_list_grant_retain(object);
  } else if (object->type == OBJECT_ENDPOINT_EXPORT) {
    retained = endpoint_export_grant_retain(object);
  } else if (object->type == OBJECT_EXECUTION_GROUP) {
    retained = execution_group_authority_retain(object, rights);
  } else if (object->type == OBJECT_OUTPUT_PRODUCER || object->type == OBJECT_OUTPUT_COLLECTOR) {
    retained = output_grant_retain((struct output_end *)object);
  } else if (object->type == OBJECT_PIPE) {
    retained = pipe_grant_retain((struct pipe_end *)object);
  } else if (object->type == OBJECT_SCREENSHOT_SAVER) {
    retained = screenshot_saver_grant_retain(object, rights);
  } else if (object->type == OBJECT_CONSOLE) {
    retained = console_authority_retain(object, rights);
  } else {
    retained = terminal_authority_retain(object, rights);
  }
  if (!retained) {
    object_release(object);
    return false;
  }
  return true;
}

void object_grant_release(struct kernel_object *object, uint64_t rights)
{
  if (object->type == OBJECT_FORWARDER || object->type == OBJECT_FORWARDER_CONTROLLER) {
    forwarder_grant_release(object, rights);
  } else if (object->type == OBJECT_READINESS_SET) {
    readiness_set_grant_release(object, rights);
  } else if (object->type == OBJECT_PROCESS_INTERRUPT) {
    process_interrupt_grant_release(object, rights);
  } else if (object->type == OBJECT_TCP || object->type == OBJECT_TCP_LISTENER) {
    tcp_grant_release(object, rights);
  } else if (object->type == OBJECT_WIFI_CONNECT || object->type == OBJECT_WIFI_LINK) {
    wifi_connection_grant_release(object, rights);
  } else if (object->type == OBJECT_WIFI_AUTH) {
    wifi_auth_grant_release(object, rights);
  } else if (object->type == OBJECT_WIFI_SCAN) {
    wifi_scan_grant_release(object);
  } else if (object->type == OBJECT_ENVIRONMENT_SNAPSHOT) {
    environment_snapshot_grant_release(object);
  } else if (object->type == OBJECT_COMMAND_LIST) {
    command_list_grant_release(object);
  } else if (object->type == OBJECT_ENDPOINT_EXPORT) {
    endpoint_export_grant_release(object);
  } else if (object->type == OBJECT_EXECUTION_GROUP) {
    execution_group_authority_release(object, rights);
  } else if (object->type == OBJECT_OUTPUT_PRODUCER || object->type == OBJECT_OUTPUT_COLLECTOR) {
    output_grant_release((struct output_end *)object);
  } else if (object->type == OBJECT_PIPE) {
    pipe_grant_release((struct pipe_end *)object);
  } else if (object->type == OBJECT_SCREENSHOT_SAVER) {
    screenshot_saver_grant_release(object, rights);
  } else if (object->type == OBJECT_CONSOLE) {
    console_authority_release(object, rights);
  } else {
    terminal_authority_release(object, rights);
  }
  object_release(object);
}

bool object_reap_pending(void)
{
  lock_retired();
  bool pending = retired_objects != NULL;
  unlock_retired();
  return pending;
}

void object_reap(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    return;
  }
  lock_retired();
  struct kernel_object *object = retired_objects;
  retired_objects = NULL;
  unlock_retired();

  while (object) {
    struct kernel_object *next = object->retired_next;
    struct execution_group *group = object->cleanup_group;
    struct execution_group *previous = object_cleanup_enter(group);
    object->destroy(object);
    object_cleanup_leave(previous);
    if (group) {
      execution_group_cleanup_end(group);
    }
    object = next;
  }
  vm_kernel_owner_release(&owner);
}
