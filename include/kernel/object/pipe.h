#ifndef KERNEL_OBJECT_PIPE_H
#define KERNEL_OBJECT_PIPE_H

#include <abi/pipe.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/object/capability.h>
#include <kernel/service/request.h>

struct pipe_pair;
struct capability_table;

struct pipe_create_request {
  struct bsp_request request;
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  struct pipe_create_reply reply;
  enum call_status result;
};

/* BSP, IF=0. Install both endpoints or unwind both; caller owns completion. */
void pipe_create_execute(struct pipe_create_request *request);

struct pipe_end {
  struct kernel_object object;
  struct pipe_pair *pair;
  bool reader;
  size_t grants; /* Open-end ownership; protected by pair lock. */
};

/* BSP, IF=0. Returned service has one owned reference. */
struct kernel_object *pipe_service_create(void);

struct syscall_result pipe_service_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result pipe_call(struct pipe_end *end, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

/* Observe a retained endpoint under the pair lock; no readiness reservation. */
uint64_t pipe_ready(struct pipe_end *end, uint64_t events);

/* IF=0, caller retains storage. Every capability/transfer grant counts,
 * including attenuated zero-right grants; operation references do not. */
bool pipe_grant_retain(struct pipe_end *end);
void pipe_grant_release(struct pipe_end *end);

#endif
