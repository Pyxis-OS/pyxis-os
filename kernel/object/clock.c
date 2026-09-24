#include <abi/clock.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/clock.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

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
  if (operation == CLOCK_NOW) {
    if (!(rights & CLOCK_RIGHT_READ)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    struct clock_reading reply;
    if (request_size || reply_capacity < sizeof(reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    reply.nanoseconds = arch_monotonic_ns();
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
  task_wait_sleep_until(wait, deadline);
  return (struct syscall_result){CALL_OK, 0};
}
