#ifndef KERNEL_MEMORY_SERVICE_H
#define KERNEL_MEMORY_SERVICE_H

#include <abi/memory.h>
#include <abi/syscall.h>
#include <kernel/mm/types.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>
#include <stdbool.h>

struct process;

struct memory_request_profile {
  bool active;
  uint64_t started_ns, published_ns;
  uint64_t service_started_ns, service_ended_ns;
};

struct memory_request {
  struct bsp_request request;
  struct process *loan; /* Inactive private address space until completion. */
  uint64_t operation;
  struct memory_region region;
  enum mm_result result;
  struct memory_request_profile profile;
};

/* Deferred publication stamps the sample after the caller leaves its private
 * root and before request queue locking. The BSP clears the process loan before
 * completion returns ownership to the caller. */
void memory_request_published(struct memory_request *request);
void memory_request_execute(struct memory_request *request);

/* BSP, IF=0. Returns one owned reference, or NULL. This stateless authority
 * object owns no process or mappings, and may be shared/transferred normally. */
struct kernel_object *memory_create(void);

/* Current user task, IF=0, live service reference and resolved rights. Captures
 * the payload, checks the allocation reply, then lends the inactive address
 * space to BSP through its scheduler. Only the resumed caller copies the reply.
 * RELEASE has no reply buffer and may unmap its captured request itself. */
struct syscall_result memory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
