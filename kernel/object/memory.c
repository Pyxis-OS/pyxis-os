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

void memory_request_published(struct memory_request *request)
{
  KASSERT(request && request->request.state == BSP_REQUEST_DEFERRED);
  if (request->profile.active) {
    request->profile.published_ns = arch_monotonic_ns();
  }
}

void memory_request_execute(struct memory_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request && request->loan);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  if (request->profile.active) {
    request->profile.service_started_ns = arch_monotonic_ns();
  }
  if (request->operation == MEMORY_ALLOCATE) {
    request->result = private_memory_allocate(request->loan, request->region.size,
        &request->region.address);
  } else {
    KASSERT(request->operation == MEMORY_RELEASE);
    request->result = private_memory_release(request->loan, request->region.address,
        request->region.size);
  }
  if (request->profile.active) {
    request->profile.service_ended_ns = arch_monotonic_ns();
  }
  request->loan = NULL;
}

static void finish_memory_profile(struct profile_snapshot *profile,
    const struct memory_request *request)
{
  uint64_t resumed = arch_monotonic_ns();
  const struct memory_request_profile *sample = &request->profile;
  struct profile_memory_operation *stats = request->operation == MEMORY_ALLOCATE ?
      &profile->allocate : &profile->release;
  profile_add(&profile->flags, &stats->requests, 1);
  profile_add(&profile->flags, &stats->requested_bytes, request->region.size);
  if (request->result == MM_OK) {
    profile_add(&profile->flags, &stats->completed_bytes, request->region.size);
  } else {
    profile_add(&profile->flags, &stats->failures, 1);
  }
  profile_duration_add(&profile->flags, &stats->publication, sample->started_ns,
      sample->published_ns);
  profile_duration_add(&profile->flags, &stats->queue, sample->published_ns,
      sample->service_started_ns);
  profile_duration_add(&profile->flags, &stats->service, sample->service_started_ns,
      sample->service_ended_ns);
  profile_duration_add(&profile->flags, &stats->resume, sample->service_ended_ns,
      resumed);
  profile_duration_add(&profile->flags, &stats->total, sample->started_ns, resumed);
}

static enum mm_result request_memory(uint64_t operation, struct memory_region *region)
{
  KASSERT(operation == MEMORY_ALLOCATE || operation == MEMORY_RELEASE);
  struct memory_request *request =
      (struct memory_request *)bsp_request_prepare(BSP_SERVICE_MEMORY);
  struct profile_snapshot *profile = task_memory_profile();
  request->profile.active = profile->flags & PROFILE_ACTIVE;
  if (request->profile.active) {
    request->profile.started_ns = arch_monotonic_ns();
  }
  request->loan = process_current();
  KASSERT(request->loan);
  request->operation = operation;
  request->region = *region;

  bsp_request_submit_and_wait(&request->request);
  if (request->profile.active) {
    finish_memory_profile(profile, request);
  }
  *region = request->region;
  enum mm_result result = request->result;
  bsp_request_release(&request->request);
  return result;
}

struct syscall_result memory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
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

  enum mm_result result = request_memory(operation, &region);
  if (result != MM_OK) {
    KASSERT(result == MM_INVALID || result == MM_NO_MEMORY);
    return (struct syscall_result){result == MM_INVALID ? CALL_BAD_REQUEST : CALL_NO_MEMORY, 0};
  }
  if (operation == MEMORY_RELEASE) {
    return (struct syscall_result){CALL_OK, 0};
  }
  /* ALLOCATE adds a disjoint region; the checked reply mapping remains valid.
   * RELEASE never touches user memory again after changing the mappings. */
  KASSERT(copy_to_user(reply_address, &region, sizeof(region)));
  return (struct syscall_result){CALL_OK, sizeof(region)};
}
