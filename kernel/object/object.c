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
#include <kernel/panic.h>
#include <stdint.h>

static struct kernel_object *retired_objects;
static atomic_bool retired_locked;

bool object_rights_valid(enum object_type type, uint64_t rights)
{
  switch (type) {
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
  case OBJECT_ENDPOINT:
    return !(rights & ~(ENDPOINT_RIGHT_CALL | ENDPOINT_RIGHT_RECEIVE |
                       ENDPOINT_RIGHT_REPLY));
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
  KASSERT(object && destroy);
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
