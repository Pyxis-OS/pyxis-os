#include <abi/clock.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/boot.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/object/clock.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <stdatomic.h>

#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)

/* Published by BSP initialization before AP startup, immutable afterward. */
static bool wall_available;
static int64_t wall_seconds;
static uint64_t wall_anchor_ns;
static atomic_uint_fast64_t cached_monotonic_ns;

_Static_assert(__atomic_always_lock_free(sizeof(cached_monotonic_ns), 0),
               "clock publication must be interrupt-safe");

void clock_cache_update(uint64_t now)
{
  uint_fast64_t previous = atomic_load_explicit(&cached_monotonic_ns, memory_order_relaxed);
  while (previous < now && !atomic_compare_exchange_weak_explicit(&cached_monotonic_ns,
      &previous, now, memory_order_release, memory_order_relaxed)) {
  }
}

uint64_t clock_coarse_ns(void)
{
  return atomic_load_explicit(&cached_monotonic_ns, memory_order_acquire);
}

void clock_init(const struct boot_info *boot)
{
  KASSERT(arch_cpu_index() == 0);
  wall_anchor_ns = arch_monotonic_ns();
  clock_cache_update(wall_anchor_ns);
  wall_seconds = boot->utc_seconds;
  wall_available = boot->utc_available;
  if (wall_available) {
    kinfo("clock: UTC seed=%ld Unix seconds; boot precision is approximate\n", wall_seconds);
  } else {
    kwarn("clock: UTC unavailable (no boot timestamp)\n");
  }
}

static bool wall_nanoseconds(uint64_t now, int64_t *result)
{
  if (!wall_available) {
    return false;
  }
  uint64_t elapsed = now > wall_anchor_ns ? now - wall_anchor_ns : 0;
  __int128 value = (__int128)wall_seconds * NANOSECONDS_PER_SECOND + elapsed;
  *result = value > INT64_MAX ? INT64_MAX : value < INT64_MIN ? INT64_MIN : (int64_t)value;
  return true;
}

bool clock_wall_nanoseconds(int64_t *result)
{
  return wall_available && wall_nanoseconds(arch_monotonic_ns(), result);
}

bool clock_wall_coarse_nanoseconds(int64_t *result)
{
  return wall_available && wall_nanoseconds(clock_coarse_ns(), result);
}

static void destroy_clock(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *clock_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_CLOCK, destroy_clock);
  }
  return object;
}

struct syscall_result clock_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == CLOCK_WALL_NOW || operation == CLOCK_WALL_COARSE_NOW) {
    uint64_t required = CLOCK_RIGHT_READ |
        (operation == CLOCK_WALL_NOW ? CLOCK_RIGHT_PRECISE : 0);
    if ((rights & required) != required) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    struct clock_wall_reading reply;
    if (request_size || reply_capacity < sizeof(reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!wall_available) {
      return (struct syscall_result){CALL_UNAVAILABLE, 0};
    }
    uint64_t now = operation == CLOCK_WALL_NOW ? arch_monotonic_ns() : clock_coarse_ns();
    uint64_t elapsed = now > wall_anchor_ns ? now - wall_anchor_ns : 0;
    /* A uint64_t nanosecond interval always fits in signed whole seconds. */
    int64_t seconds = elapsed / NANOSECONDS_PER_SECOND;
    if (wall_seconds > INT64_MAX - seconds) {
      return (struct syscall_result){CALL_LIMIT, 0};
    }
    reply.seconds = wall_seconds + seconds;
    reply.nanoseconds = elapsed % NANOSECONDS_PER_SECOND;
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (operation == CLOCK_NOW || operation == CLOCK_COARSE_NOW) {
    uint64_t required = CLOCK_RIGHT_READ |
        (operation == CLOCK_NOW ? CLOCK_RIGHT_PRECISE : 0);
    if ((rights & required) != required) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    struct clock_reading reply;
    if (request_size || reply_capacity < sizeof(reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    reply.nanoseconds = operation == CLOCK_NOW ? arch_monotonic_ns() : clock_coarse_ns();
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (operation != CLOCK_SLEEP_UNTIL) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & CLOCK_RIGHT_SLEEP)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  uint64_t deadline;
  if (request_size != sizeof(deadline)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&deadline, request_address, sizeof(deadline))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct task_wait *wait = task_wait_prepare();
  if (!task_wait_sleep_until_interruptible(wait, deadline)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  return (struct syscall_result){CALL_OK, 0};
}
