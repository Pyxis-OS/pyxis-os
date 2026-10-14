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
#include <kernel/process_vm.h>
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

static bool allocates_memory(uint64_t operation)
{
  return operation == MEMORY_ALLOCATE || operation == MEMORY_ALLOCATE_GUARDED;
}

bool memory_request_stepped(const struct memory_request *request)
{
  return allocates_memory(request->operation) &&
      request->region.size > PRIVATE_MEMORY_STEP_PAGES * PAGE_SIZE;
}

static size_t allocation_reply_size(uint64_t operation)
{
  return operation == MEMORY_ALLOCATE_GUARDED ?
      sizeof(struct memory_reservation) : sizeof(struct memory_region);
}

static void record_memory_profile(struct profile_snapshot *profile, uint64_t operation,
    const struct memory_region *region, enum mm_result result, uint64_t started_ns,
    uint64_t service_started_ns, uint64_t service_ended_ns)
{
  uint64_t ended = arch_monotonic_ns();
  struct profile_memory_operation *stats = allocates_memory(operation) ?
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

void memory_request_execute(struct memory_request *request)
{
  KASSERT(request->request.state == BSP_REQUEST_FORWARDED && request->process);
  process_vm_require_mutation(request->process);
  request->service_started_ns = request->profiled ? arch_monotonic_ns() : 0;
  if (allocates_memory(request->operation) &&
      !process_vm_buffer_check(request->process, request->reply_address,
        allocation_reply_size(request->operation), true)) {
    request->result = MM_INVALID;
    request->status = CALL_BAD_BUFFER;
    request->service_ended_ns = request->profiled ? arch_monotonic_ns() : 0;
    request->process = NULL;
    return;
  }
  if (request->operation == MEMORY_ALLOCATE_GUARDED) {
    request->status = private_memory_allocate_guarded(request->process, request->region.size,
        &request->reservation);
    request->result = request->status == CALL_OK ? MM_OK : MM_NO_MEMORY;
  } else if (request->operation == MEMORY_RELEASE_RESERVATION) {
    request->status = private_memory_release_reservation(request->process,
        request->reservation.identity);
    request->result = request->status == CALL_OK ? MM_OK : MM_INVALID;
  } else {
    request->result = request->operation == MEMORY_ALLOCATE ?
      private_memory_allocate(request->process, request->region.size, &request->region.address) :
      private_memory_release(request->process, request->region.address, request->region.size);
    KASSERT(request->result == MM_OK || request->result == MM_INVALID ||
        request->result == MM_NO_MEMORY);
    request->status = request->result == MM_OK ? CALL_OK :
      request->result == MM_INVALID ? CALL_BAD_REQUEST : CALL_NO_MEMORY;
  }
  request->service_ended_ns = request->profiled ? arch_monotonic_ns() : 0;
  request->process = NULL;
}

struct syscall_result memory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct profile_snapshot *profile = profile_memory_current();
  bool profiled = profile->flags & PROFILE_ACTIVE;
  uint64_t started_ns = profiled ? arch_monotonic_ns() : 0;

  if (operation != MEMORY_ALLOCATE && operation != MEMORY_RELEASE &&
      operation != MEMORY_ALLOCATE_GUARDED && operation != MEMORY_RELEASE_RESERVATION) {
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

  struct memory_region region = {0};
  struct memory_reservation reservation = {0};
  if (allocates_memory(operation)) {
    size_t size = request.allocate.size;
    size_t reply_size = allocation_reply_size(operation);
    if (!size || size > SIZE_MAX - (PAGE_SIZE - 1) || reply_capacity < reply_size ||
        (operation == MEMORY_ALLOCATE_GUARDED && request.allocate_guarded.reserved)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (operation == MEMORY_ALLOCATE_GUARDED && size > SIZE_MAX - PAGE_SIZE) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    region.size = size;
  } else if (operation == MEMORY_RELEASE_RESERVATION) {
    if (!request.release_reservation.identity || request.release_reservation.reserved) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    reservation.identity = request.release_reservation.identity;
  } else {
    region = request.release;
    if (!region.address || !region.size || (region.address & (PAGE_SIZE - 1)) ||
        (region.size & (PAGE_SIZE - 1)) || region.size > UINTPTR_MAX - region.address) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }

  struct process *process = process_current();
  KASSERT(process);
  struct memory_request *pending = (void *)bsp_request_prepare(BSP_SERVICE_MEMORY);
  pending->process = process;
  pending->operation = operation;
  pending->region = region;
  pending->reservation = reservation;
  pending->reply_address = reply_address;
  pending->profiled = profiled;
  pending->result = MM_INVALID;
  pending->request.vm_process = process;
  pending->request.vm_mutator = task_current_user();
  bsp_request_submit_and_wait(&pending->request);
  region = pending->region;
  reservation = pending->reservation;
  enum mm_result result = pending->result;
  uint64_t service_started_ns = pending->service_started_ns;
  uint64_t service_ended_ns = pending->service_ended_ns;
  bool reply_owned = pending->request.vm_reply_owned;
  struct syscall_result reply = {pending->status, 0};
  bsp_request_release(&pending->request);
  if (reply.status == CALL_OK && allocates_memory(operation)) {
    /* BSP rechecked this output before effects; REPLY ownership prevents
     * intervening mapping changes until the final copy has finished. */
    if (operation == MEMORY_ALLOCATE_GUARDED) {
      KASSERT(copy_to_user(reply_address, &reservation, sizeof(reservation)));
      reply.reply_size = sizeof(reservation);
    } else {
      KASSERT(copy_to_user(reply_address, &region, sizeof(region)));
      reply.reply_size = sizeof(region);
    }
  }
  if (reply_owned) {
    /* RELEASE performs only kernel cleanup after unmapping its captured input. */
    process_vm_reply_finish(process);
  }
  if (profiled) {
    record_memory_profile(profile, operation, &region, result, started_ns,
        service_started_ns, service_ended_ns);
  }
  return reply;
}
