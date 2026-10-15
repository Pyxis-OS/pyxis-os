#ifndef KERNEL_OBJECT_OUTPUT_H
#define KERNEL_OBJECT_OUTPUT_H

#include <abi/output.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct process;
struct output_pair;

struct output_end {
  struct kernel_object object;
  struct output_pair *pair;
  bool collector;
};

enum output_request_operation {
  OUTPUT_REQUEST_CREATE,
  OUTPUT_REQUEST_TAKE,
};

struct output_request {
  struct bsp_request request;
  enum output_request_operation operation;
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  struct process *owner;
  struct output_pair *pair; /* Borrowed from the parked caller's admitted end. */
  struct output_create_reply created;
  struct output_take_reply taken;
  enum call_status status;
};

void output_execute(struct output_request *request);
struct syscall_result output_create_call(size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result output_producer_call(struct output_end *end, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result output_collector_call(struct output_end *end, uint64_t rights,
    uint64_t operation, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity);
bool output_grant_retain(struct output_end *end);
void output_grant_release(struct output_end *end);
bool output_collector_owned(const struct kernel_object *object, struct process *caller);
uint64_t output_ready(struct output_end *end, uint64_t events);

#endif
