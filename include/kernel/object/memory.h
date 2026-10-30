#ifndef KERNEL_MEMORY_SERVICE_H
#define KERNEL_MEMORY_SERVICE_H

#include <abi/memory.h>
#include <abi/syscall.h>
#include <abi/shared_memory.h>
#include <kernel/mm/types.h>
#include <kernel/object/object.h>
#include <kernel/object/capability.h>
#include <kernel/service/request.h>

struct process;
struct shared_region;

struct memory_request {
  struct bsp_request request;
  struct process *process;
  uint64_t operation;
  struct memory_region region;
  struct memory_reservation reservation;
  struct memory_subrange subrange;
  struct shared_region *shared_region; /* Borrowed through the admitted MAP call. */
  struct capability_reservation handle_reservation;
  struct capability_reserved_slot handle_slot;
  struct shared_memory_created created;
  bool shared_writable;
  size_t alignment;
  uintptr_t reply_address;
  enum mm_result result;
  enum call_status status;
  bool profiled;
  uint64_t service_started_ns, service_ended_ns;
};

void memory_request_execute(struct memory_request *request);

/* BSP, IF=0. Returns one owned reference, or NULL. This stateless authority
 * object owns no process or mappings, and may be shared/transferred normally. */
struct kernel_object *memory_create(void);

/* Current user task, IF=0, live service reference and resolved rights. Captures
 * the payload and checks the allocation reply, then changes the caller's own
 * private address space through a caller-CPU quiescent mutation transaction. RELEASE has
 * no reply buffer and may unmap its captured request itself. */
struct syscall_result memory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

/* Same private mutation/reply ownership as MEMORY; REGION is a retained leaf
 * from the caller's resolved grant. Mapping retains its source lineage. */
struct syscall_result shared_memory_call(struct shared_region *region,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);

#endif
