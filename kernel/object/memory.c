#include <abi/memory.h>
#include <abi/profile.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/object/memory.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/service/profile.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

_Static_assert(MEMORY_PAGE_SIZE == PAGE_SIZE, "memory ABI page size");

static void destroy_memory(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *memory_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_MEMORY, destroy_memory);
  }
  return object;
}

static void record_memory_profile(struct profile_snapshot *profile, uint64_t operation,
    const struct memory_region *region, enum mm_result result, uint64_t started_ns,
    uint64_t service_started_ns, uint64_t service_ended_ns)
{
  uint64_t ended = arch_monotonic_ns();
  struct profile_memory_operation *stats = operation == MEMORY_ALLOCATE ?
      &profile->allocate : &profile->release;
  profile_add(&profile->flags, &stats->requests, 1);
  profile_add(&profile->flags, &stats->requested_bytes, region->size);
  if (result == MM_OK) {
    profile_add(&profile->flags, &stats->completed_bytes, region->size);
  } else {
    profile_add(&profile->flags, &stats->failures, 1);
  }
  profile_duration_add(&profile->flags, &stats->service, service_started_ns,
      service_ended_ns);
  profile_duration_add(&profile->flags, &stats->total, started_ns, ended);
}

struct syscall_result memory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct profile_snapshot *profile = profile_memory_current();
  bool profiled = profile->flags & PROFILE_ACTIVE;
  uint64_t started_ns = profiled ? arch_monotonic_ns() : 0;

  if (operation != MEMORY_ALLOCATE && operation != MEMORY_RELEASE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & MEMORY_RIGHT_MANAGE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  union memory_payload request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct memory_region region;
  if (operation == MEMORY_ALLOCATE) {
    size_t size = request.allocate.size;
    if (!size || size > SIZE_MAX - (PAGE_SIZE - 1) || reply_capacity < sizeof(region)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(region), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    region = (struct memory_region){.size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)};
  } else {
    region = request.release;
    if (!region.address || !region.size || (region.address & (PAGE_SIZE - 1)) ||
        (region.size & (PAGE_SIZE - 1)) || region.size > UINTPTR_MAX - region.address) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }

  /* This task is the process's only one and runs here with IF=0, so no other
   * CPU uses or caches its address space while the mappings change. */
  struct process *process = process_current();
  KASSERT(process);
  uint64_t service_started_ns = profiled ? arch_monotonic_ns() : 0;
  enum mm_result result = operation == MEMORY_ALLOCATE ?
      private_memory_allocate(process, region.size, &region.address) :
      private_memory_release(process, region.address, region.size);
  uint64_t service_ended_ns = profiled ? arch_monotonic_ns() : 0;

  struct syscall_result reply = {CALL_OK, 0};
  if (result != MM_OK) {
    KASSERT(result == MM_INVALID || result == MM_NO_MEMORY);
    reply.status = result == MM_INVALID ? CALL_BAD_REQUEST : CALL_NO_MEMORY;
  } else if (operation == MEMORY_ALLOCATE) {
    /* ALLOCATE adds a disjoint region; the checked reply mapping remains valid.
     * RELEASE never touches user memory again after changing the mappings. */
    KASSERT(copy_to_user(reply_address, &region, sizeof(region)));
    reply.reply_size = sizeof(region);
  }
  if (profiled) {
    record_memory_profile(profile, operation, &region, result, started_ns,
        service_started_ns, service_ended_ns);
  }
  return reply;
}
