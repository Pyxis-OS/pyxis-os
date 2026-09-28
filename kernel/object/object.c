#include <abi/namespace.h>
#include <abi/profile.h>
#include <abi/pipe.h>
#include <abi/space.h>
#include <abi/udp.h>
#include <abi/tcp.h>
#include <abi/random.h>
#include <abi/mount.h>
#include <abi/echo.h>
#include <abi/net_config.h>
#include <abi/keyboard.h>
#include <abi/clock.h>
#include <abi/display.h>
#include <abi/file.h>
#include <abi/process.h>
#include <abi/launcher.h>
#include <abi/memory.h>
#include <abi/directory.h>
#include <abi/console.h>
#include <abi/endpoint.h>
#include <arch/smp.h>
#include <kernel/object/object.h>
#include <kernel/object/endpoint.h>
#include <kernel/panic.h>
#include <stdint.h>

static struct kernel_object *retired_objects;
static atomic_bool retired_locked;

uint64_t object_protocol(const struct kernel_object *object)
{
  switch (object->type) {
  case OBJECT_CONSOLE:
    return PROTOCOL_CONSOLE;
  case OBJECT_FILE:
    return PROTOCOL_FILE;
  case OBJECT_ENDPOINT:
    return PROTOCOL_ENDPOINT;
  case OBJECT_DIRECTORY:
    return PROTOCOL_DIRECTORY;
  case OBJECT_MEMORY:
    return PROTOCOL_MEMORY;
  case OBJECT_PROCESS_CONTROL:
    return PROTOCOL_PROCESS;
  case OBJECT_LAUNCHER:
    return PROTOCOL_LAUNCHER;
  case OBJECT_DISPLAY:
    return PROTOCOL_DISPLAY;
  case OBJECT_CLOCK:
    return PROTOCOL_CLOCK;
  case OBJECT_KEYBOARD:
    return PROTOCOL_KEYBOARD;
  case OBJECT_MOUNT:
    return PROTOCOL_MOUNT;
  case OBJECT_ECHO:
    return PROTOCOL_ECHO;
  case OBJECT_NET_CONFIG:
    return PROTOCOL_NET_CONFIG;
  case OBJECT_UDP_SERVICE:
    return PROTOCOL_UDP_SERVICE;
  case OBJECT_UDP:
    return PROTOCOL_UDP;
  case OBJECT_RANDOM:
    return PROTOCOL_RANDOM;
  case OBJECT_TCP_SERVICE:
    return PROTOCOL_TCP_SERVICE;
  case OBJECT_TCP:
    return PROTOCOL_TCP;
  case OBJECT_SPACE:
    return PROTOCOL_SPACE;
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
  case OBJECT_NAMESPACE_SERVICE:
    return PROTOCOL_NAMESPACE_SERVICE;
  case OBJECT_NAMESPACE:
    return PROTOCOL_NAMESPACE;
  case OBJECT_ENDPOINT_EXPORT:
    return endpoint_export_protocol(object);
  default:
    return 0;
  }
}

bool object_stream_valid(const struct kernel_object *object, uint64_t protocol,
                         uint64_t transport)
{
  if (object->type == OBJECT_ENDPOINT_EXPORT) {
    return protocol == PROTOCOL_FILE && object_protocol(object) == PROTOCOL_FILE &&
        transport == HANDLE_TRANSPORT_CALL;
  }
  if (transport) {
    return false;
  }
  return (protocol == PROTOCOL_FILE && object->type == OBJECT_FILE) ||
      (protocol == PROTOCOL_CONSOLE && object->type == OBJECT_CONSOLE) ||
      (protocol == PROTOCOL_PIPE && object->type == OBJECT_PIPE);
}

bool object_authority_valid(const struct kernel_object *object, uint64_t rights,
                            uint64_t transport)
{
  if (!object) {
    return false;
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
  case OBJECT_NAMESPACE_SERVICE:
    return !(rights & ~NAMESPACE_SERVICE_RIGHT_CREATE);
  case OBJECT_NAMESPACE:
    return !(rights & ~NAMESPACE_RIGHTS);
  case OBJECT_PIPE_SERVICE:
    return !(rights & ~PIPE_SERVICE_RIGHT_CREATE);
  case OBJECT_PIPE:
    return !(rights & ~(PIPE_RIGHT_READ | PIPE_RIGHT_WRITE));
  case OBJECT_PROFILE:
    return !(rights & ~(PROFILE_RIGHT_MEMORY | PROFILE_RIGHT_FILE));
  case OBJECT_SPACE:
    return !(rights & ~SPACE_RIGHT_SET_TITLE);
  case OBJECT_CONSOLE:
    return !(rights & ~CONSOLE_RIGHTS);
  case OBJECT_FILE:
    return !(rights & ~FILE_RIGHTS);
  case OBJECT_DIRECTORY:
    return !(rights & ~DIRECTORY_RIGHTS);
  case OBJECT_MEMORY:
    return !(rights & ~MEMORY_RIGHT_MANAGE);
  case OBJECT_PROCESS_CONTROL:
    return !(rights & ~PROCESS_RIGHT_WAIT);
  case OBJECT_KEYBOARD:
    return !(rights & ~KEYBOARD_RIGHT_INPUT);
  case OBJECT_MOUNT:
    return !(rights & ~MOUNT_RIGHT_OPEN_ROOT);
  case OBJECT_RANDOM:
    return !(rights & ~RANDOM_RIGHT_READ);
  case OBJECT_TCP_SERVICE:
    return !(rights & ~TCP_SERVICE_RIGHT_CONNECT);
  case OBJECT_TCP:
    return !(rights & ~TCP_RIGHTS);
  case OBJECT_UDP_SERVICE:
    return !(rights & ~UDP_SERVICE_RIGHT_OPEN);
  case OBJECT_UDP:
    return !(rights & ~UDP_RIGHTS);
  case OBJECT_NET_CONFIG:
    return !(rights & ~NET_CONFIG_RIGHTS);
  case OBJECT_ECHO:
    return !(rights & ~ECHO_RIGHT_SEND);
  case OBJECT_CLOCK:
    return !(rights & ~CLOCK_RIGHTS);
  case OBJECT_DISPLAY:
    return !(rights & ~DISPLAY_RIGHT_DRAW);
  case OBJECT_LAUNCHER:
    return !(rights & ~LAUNCHER_RIGHT_LAUNCH);
  case OBJECT_ENDPOINT_SERVICE:
    return !(rights & ~ENDPOINT_SERVICE_RIGHT_CREATE);
  case OBJECT_ENDPOINT_RECEIPT:
    return !(rights & ~ENDPOINT_RIGHT_REPLY);
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

void object_release(struct kernel_object *object)
{
  size_t previous = atomic_fetch_sub_explicit(&object->references, 1,
                                              memory_order_acq_rel);
  KASSERT(previous);
  if (previous != 1) {
    return;
  }

  if (object->type == OBJECT_ENDPOINT_RECEIPT) {
    endpoint_receipt_release(object);
    return;
  }

  /* The last owner lends the object's own link to the queue. Even an AP can
   * retire it without allocating a queue node or touching the heap. */
  lock_retired();
  object->retired_next = retired_objects;
  retired_objects = object;
  unlock_retired();
  /* The BSP may destroy object immediately after publication. */
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
  lock_retired();
  struct kernel_object *object = retired_objects;
  retired_objects = NULL;
  unlock_retired();

  while (object) {
    struct kernel_object *next = object->retired_next;
    object->destroy(object);
    object = next;
  }
}
