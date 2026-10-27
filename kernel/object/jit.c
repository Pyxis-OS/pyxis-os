#include <abi/jit.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/object/jit.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>
#include <kernel/service/profile.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

_Static_assert(JIT_PAGE_SIZE == PAGE_SIZE, "JIT ABI page size");

static void destroy_jit(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *jit_create(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_JIT, destroy_jit);
  }
  return object;
}

void jit_request_execute(struct jit_request *request)
{
  KASSERT(request->request.state == BSP_REQUEST_FORWARDED && request->process);
  process_vm_require_mutation(request->process);
  request->service_started_ns = request->profiled ? arch_monotonic_ns() : 0;
  if (request->operation == JIT_ALLOCATE) {
    if (!process_vm_buffer_check(request->process, request->reply_address,
        sizeof(request->region), true)) {
      request->status = CALL_BAD_BUFFER;
    } else {
      /* The optional hint promises no placement; the initial allocator ignores it. */
      request->status = private_jit_allocate(request->process,
          request->payload.allocate.size, &request->region);
    }
  } else if (request->operation == JIT_PROTECT) {
    request->status = private_jit_protect(request->process,
        request->payload.protect.identity, request->payload.protect.offset,
        request->payload.protect.size, request->payload.protect.state);
  } else {
    KASSERT(request->operation == JIT_RELEASE);
    size_t bytes;
    request->status = private_jit_release(request->process, request->payload.release.identity, &bytes);
    request->region.size = bytes;
  }
  request->service_ended_ns = request->profiled ? arch_monotonic_ns() : 0;
  request->process = NULL;
}

static void record_profile(struct profile_snapshot *profile, uint64_t operation,
    size_t bytes, enum call_status status, uint64_t started,
    uint64_t service_started, uint64_t service_ended)
{
  uint64_t ended = arch_monotonic_ns();
  struct profile_memory_operation *stats = operation == JIT_ALLOCATE ?
      &profile->jit.allocate : operation == JIT_PROTECT ?
      &profile->jit.protect : &profile->jit.release;
  profile_add(&profile->flags, &stats->requests, 1);
  profile_add(&profile->flags, &stats->requested_bytes, bytes);
  if (status == CALL_OK) {
    profile_add(&profile->flags, &stats->completed_bytes, bytes);
  } else {
    profile_add(&profile->flags, &stats->failures, 1);
  }
  profile_duration_add(&profile->flags, &stats->service, service_started, service_ended);
  profile_duration_add(&profile->flags, &stats->total, started, ended);
}

struct syscall_result jit_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct profile_snapshot *profile = profile_memory_current();
  bool profiled = profile->flags & PROFILE_ACTIVE;
  uint64_t started = profiled ? arch_monotonic_ns() : 0;
  if (!(rights & JIT_RIGHT_MANAGE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation != JIT_ALLOCATE && operation != JIT_PROTECT && operation != JIT_RELEASE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (request_size != sizeof(union jit_payload)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  union jit_payload payload;
  if (!copy_from_user(&payload, request_address, sizeof(payload))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == JIT_ALLOCATE) {
    if (!payload.allocate.size || payload.allocate.size > SIZE_MAX - (PAGE_SIZE - 1) ||
        payload.allocate.reserved[0] || payload.allocate.reserved[1] ||
        reply_capacity < sizeof(struct jit_region)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    payload.allocate.size = (payload.allocate.size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (!user_buffer_check(reply_address, sizeof(struct jit_region), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  } else if (operation == JIT_PROTECT) {
    if (!payload.protect.identity || !payload.protect.size ||
        (payload.protect.offset & (PAGE_SIZE - 1)) ||
        (payload.protect.size & (PAGE_SIZE - 1)) ||
        payload.protect.offset > SIZE_MAX - payload.protect.size ||
        (payload.protect.state != JIT_WRITABLE && payload.protect.state != JIT_READ_ONLY &&
         payload.protect.state != JIT_EXECUTABLE)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  } else if (!payload.release.identity || payload.release.reserved[0] ||
      payload.release.reserved[1] || payload.release.reserved[2]) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct process *process = process_current();
  struct jit_request *pending = (void *)bsp_request_prepare(BSP_SERVICE_JIT);
  pending->process = process;
  pending->operation = operation;
  pending->payload = payload;
  pending->reply_address = reply_address;
  pending->profiled = profiled;
  pending->region = (struct jit_region){0};
  pending->status = CALL_BAD_REQUEST;
  pending->service_started_ns = pending->service_ended_ns = 0;
  pending->request.vm_process = process;
  pending->request.vm_mutator = task_current_user();
  process_vm_private_call(&pending->request);
  struct jit_region region = pending->region;
  bool reply_owned = pending->request.vm_reply_owned;
  uint64_t service_started = pending->service_started_ns;
  uint64_t service_ended = pending->service_ended_ns;
  struct syscall_result reply = {pending->status, 0};
  bsp_request_release(&pending->request);
  if (reply.status == CALL_OK && operation == JIT_ALLOCATE) {
    KASSERT(copy_to_user(reply_address, &region, sizeof(region)));
    reply.reply_size = sizeof(region);
  }
  if (reply_owned) {
    process_vm_reply_finish(process);
  }
  if (profiled) {
    size_t bytes = operation == JIT_ALLOCATE ? payload.allocate.size :
        operation == JIT_PROTECT ? payload.protect.size : region.size;
    record_profile(profile, operation, bytes, reply.status, started,
        service_started, service_ended);
  }
  return reply;
}
