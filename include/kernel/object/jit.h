#ifndef KERNEL_OBJECT_JIT_H
#define KERNEL_OBJECT_JIT_H

#include <abi/jit.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct process;

struct jit_request {
  struct bsp_request request;
  struct process *process;
  uint64_t operation;
  union jit_payload payload;
  struct jit_region region;
  uintptr_t reply_address;
  enum call_status status;
  bool profiled;
  uint64_t service_started_ns, service_ended_ns;
};

/* BSP preparation, IF=0. Stateless authority, no process or region ownership. */
struct kernel_object *jit_create(void);
void jit_request_execute(struct jit_request *request);
/* Current admitted user task, IF=0. Captures input before caller-CPU mutation. */
struct syscall_result jit_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
