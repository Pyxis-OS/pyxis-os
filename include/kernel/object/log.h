#ifndef KERNEL_OBJECT_LOG_H
#define KERNEL_OBJECT_LOG_H

#include <kernel/object/object.h>
#include <kernel/service/request.h>
#include <kernel/syscall.h>

/* BSP, IF=0. One owned reference to stateless read-only log authority. The
 * static ring outlives this object and every delegated/copy handle. */
struct kernel_object *log_create(void);

struct bsp_request;
struct space;
struct log_stream_create_request {
  struct bsp_request request;
  struct space *space;
  uint64_t level;
  struct kernel_object *created;
  enum call_status status;
};

/* BSP, IF=0. Allocates the stream (an unlinked object with no grant yet). */
void log_stream_create_execute(struct log_stream_create_request *request);
struct syscall_result log_stream_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result log_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
