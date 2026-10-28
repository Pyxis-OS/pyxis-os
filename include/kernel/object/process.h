#ifndef KERNEL_PROCESS_CONTROL_H
#define KERNEL_PROCESS_CONTROL_H

#include <abi/process.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>
#include <kernel/wait.h>

struct process;
struct process_interrupt;
struct readiness_source;

/* Retains a result and waiters, never process storage or its address space.
 * The borrowed process link is cleared under this lock before process freeing. */
struct process_control {
  struct kernel_object object;
  atomic_bool locked;
  bool complete;
  struct process_result result;
  struct task_wait_link *waiters;
  struct process *process;
  struct process_interrupt *interrupt; /* Weak, attached until last RECEIVE close. */
};

struct process_interrupt_create_request {
  struct bsp_request request;
  struct process *caller;
  struct kernel_object *created;
  enum call_status status;
};

/* IF=0. Returns one owned reference or NULL. An unsubmitted process may
 * release this without completing it; no observer may be running at that point. */
struct process_control *process_control_create(void);

/* IF=0. Exclusive unpublished actor attaches before any observer can be used.
 * The retained retirement owner detaches before process destruction. */
void process_control_attach_process(struct process_control *control, struct process *process);
void process_control_detach_process(struct process_control *control);

/* IF=0, with an owned reference. Publish once, only after execution
 * resources are reclaimed. Detaches and wakes every waiter. */
void process_control_complete(struct process_control *control, struct process_result result);

/* Retained control object; masks interrupts while sampling completion. */
uint64_t process_control_ready(struct process_control *control);
bool process_control_stopped(struct process_control *control);

/* Current process, IF=0, with an admitted storage reference and checked protocol.
 * WAIT may sleep; revalidate output after resumption, before copying it. */
struct syscall_result process_control_call(struct process_control *control,
    uint64_t rights, uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

/* Creation allocates unlinked storage through the existing service catalog;
 * publish attaches only after the handle reply has been copied successfully. */
void process_interrupt_create_execute(struct process_interrupt_create_request *request);
enum call_status process_interrupt_publish(struct kernel_object *object, struct process *caller);
void process_interrupt_abandon(struct kernel_object *object);
struct syscall_result process_interrupt_create_call(uintptr_t reply, uint64_t capacity);
struct syscall_result process_interrupt_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t request_size);
bool process_interrupt_grant_retain(struct kernel_object *object, uint64_t rights);
void process_interrupt_grant_release(struct kernel_object *object, uint64_t rights);
bool process_interrupt_owned(const struct kernel_object *object, struct process *caller);
uint64_t process_interrupt_ready(struct kernel_object *object);
struct readiness_source *process_interrupt_readiness_source(struct kernel_object *object);

#endif
