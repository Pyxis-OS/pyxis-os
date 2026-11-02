#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/process_view.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/user_memory.h>

static void destroy_process_view(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *process_view_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_PROCESS_VIEW, destroy_process_view);
  }
  return object;
}

/* Records one listing would hold. Each space is its own pass, as in the listing. */
static uint64_t count_records(struct space *only)
{
  uint64_t total = 0;
  struct space *space = only ? only : space_caelum();
  while (space) {
    uint64_t space_total;
    execution_group_list(space, NULL, 0, &space_total);
    total += space_total;
    space = only ? NULL : __atomic_load_n(&space->next, __ATOMIC_ACQUIRE);
  }
  return total;
}

/* ONLY lists one space; NULL lists every published space in registry order.
 * Each space is one consistent pass; the reply is copied out after all guards
 * drop. The record buffer is sized to what exists (counted just before), not to
 * the caller's capacity: a caller that asks for more than there is gets no more
 * memory allocated, and records created after the count are reported by `total`
 * with PROCESS_LIST_TRUNCATED. */
static struct syscall_result list_processes(struct space *only, uintptr_t reply_address,
    uint64_t reply_capacity)
{
  KASSERT(!cpu_interrupts_enabled());
  if (reply_capacity < sizeof(struct process_list_header)) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }
  uint64_t room_wanted = (reply_capacity - sizeof(struct process_list_header)) /
      sizeof(struct process_list_record);
  uint64_t existing = count_records(only);
  size_t room = room_wanted < existing ? (size_t)room_wanted : (size_t)existing;
  size_t used = sizeof(struct process_list_header) + room * sizeof(struct process_list_record);
  if (!user_buffer_check(reply_address, used, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct process_list_record *records = NULL;
  if (room) {
    records = kmalloc(room * sizeof(*records));
    if (!records) {
      return (struct syscall_result){CALL_NO_MEMORY, 0};
    }
  }

  size_t filled = 0;
  uint64_t total = 0;
  struct space *space = only ? only : space_caelum();
  while (space) {
    uint64_t space_total;
    filled += execution_group_list(space, records + filled, room - filled, &space_total);
    total += space_total;
    space = only ? NULL : __atomic_load_n(&space->next, __ATOMIC_ACQUIRE);
  }

  struct process_list_header header = {
    .version = PROCESS_LIST_VERSION,
    .record_size = sizeof(struct process_list_record),
    .total = total,
    .returned = filled,
    .flags = (total > filled ? PROCESS_LIST_TRUNCATED : 0) |
        (arch_tsc_cycles_ns(UINT64_C(1) << 32) ? 0 : PROCESS_LIST_CPU_UNAVAILABLE),
    .monotonic_ns = arch_monotonic_ns(),
  };
  size_t record_bytes = filled * sizeof(*records);
  bool copied = copy_to_user(reply_address, &header, sizeof(header)) &&
      (!record_bytes || copy_to_user(reply_address + sizeof(header), records, record_bytes));
  kfree(records);
  if (!copied) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(header) + record_bytes};
}

struct syscall_result process_view_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != PROCESS_VIEW_LIST) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & PROCESS_VIEW_RIGHT_LIST)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  return list_processes(NULL, reply_address, reply_capacity);
}

struct syscall_result space_processes_call(uintptr_t reply_address, uint64_t reply_capacity)
{
  struct process *process = process_current();
  KASSERT(process);
  return list_processes(process->space, reply_address, reply_capacity);
}
