#ifndef KERNEL_THREAD_H
#define KERNEL_THREAD_H

#include <abi/thread.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>
#include <kernel/wait.h>

struct process;
struct task;
struct user_buffer_lease;

struct thread_control {
  struct kernel_object object;
  atomic_bool locked;
  bool complete;
  struct thread_result result;
  struct task_wait_link *waiters;
};

struct thread_request {
  struct bsp_request request;
  struct process *process;
  struct task *caller;
  uint64_t operation;
  struct thread_create input;
  struct thread_fs_context fs;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  uintptr_t reply_address;
  struct thread_reply reply;
  enum call_status status;
};

/* IF=0, unpublished allocation; observer storage owns no process/task/VM pointer.
 * Completion retains the control and uses its guard after actual task reclamation. */
struct kernel_object *thread_service_create(void);
struct thread_control *thread_control_create(void);
void thread_control_complete(struct thread_control *control, struct thread_result result);
uint64_t thread_control_ready(struct thread_control *control);
struct syscall_result thread_control_call(struct thread_control *control,
    uint64_t rights, uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result thread_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
/* Consumes the current-process packet lease on every path. WAIT releases it
 * before sleeping; WAKE keeps it through the final checked reply copy. */
struct syscall_result thread_address_packet_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, struct user_buffer_lease *lease);
void thread_request_execute(struct thread_request *request);

#endif
