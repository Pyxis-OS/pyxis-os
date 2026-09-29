#ifndef KERNEL_OBJECT_PIPE_H
#define KERNEL_OBJECT_PIPE_H

#include <abi/pipe.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct pipe_pair;
struct capability_table;

struct pipe_create_request {
  struct bsp_request request;
  struct capability_table *table; /* Exclusive loan until completion. */
  struct pipe_create_reply reply;
  enum call_status result;
};

/* BSP, IF=0. Install both endpoints or unwind both; caller owns completion. */
void pipe_create_execute(struct pipe_create_request *request);

struct pipe_end {
  struct kernel_object object;
  struct pipe_pair *pair;
  bool reader;
};

/* BSP, IF=0. Returned service has one owned reference. */
struct kernel_object *pipe_service_create(void);

struct syscall_result pipe_service_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result pipe_call(struct pipe_end *end, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
