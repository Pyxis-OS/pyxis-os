#ifndef KERNEL_OBJECT_TERMINAL_H
#define KERNEL_OBJECT_TERMINAL_H

#include <abi/terminal.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct capability_table;

struct terminal_create_service_request {
  struct bsp_request request;
  struct capability_table *table; /* Exclusive loan through completion. */
  uint64_t columns, rows;
  struct terminal_create_reply reply;
  enum call_status result;
};

/* BSP, IF=0. Install all four capabilities atomically or release the session. */
void terminal_create_execute(struct terminal_create_service_request *request);
struct kernel_object *terminal_service_create(void);

/* IF=0, held storage reference. Capability entries and captured IPC grants
 * each retain their logical authority separately. Ordinary object references,
 * including readiness observations, never keep a direction/controller open.
 * Other object types are a successful no-op. Release precedes object_release. */
bool terminal_authority_retain(struct kernel_object *object, uint64_t rights);
void terminal_authority_release(struct kernel_object *object, uint64_t rights);

/* Any CPU, preserves IF. Snapshot under the lock; reserves no queue capacity. */
uint64_t terminal_attachment_ready(struct kernel_object *object, uint64_t events);
/* Any CPU, preserves IF. Application input/output snapshot; input admission
 * reserves no reader slot. Geometry is independent and stays at generation 1. */
uint64_t terminal_application_ready(struct kernel_object *object, uint64_t events,
    uint64_t observed_generation);

struct syscall_result terminal_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result terminal_application_call(struct kernel_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result terminal_attachment_call(struct kernel_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);
struct syscall_result terminal_events_call(struct kernel_object *object,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size);

#endif
