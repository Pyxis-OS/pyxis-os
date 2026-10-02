#ifndef KERNEL_PROCESS_CONTROL_H
#define KERNEL_PROCESS_CONTROL_H

#include <abi/process.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/wait.h>

struct task;

/* Retains a result and waiters, never the process or its address space. The
 * task link is borrowed: it is set while the task's memory is live and cleared
 * under this lock before the task can be freed. */
struct process_control {
  struct kernel_object object;
  atomic_bool locked;
  bool complete;
  struct process_result result;
  struct task_wait_link *waiters;
  struct task *task;
};

/* BSP, IF=0. Returns one owned reference or NULL. An unsubmitted process may
 * release this without completing it; no observer may be running at that point. */
struct process_control *process_control_create(void);

/* BSP, IF=0. Attach a prepared task before its observer can be used, and
 * detach it before the task is freed, including discarded preparations. */
void process_control_attach_task(struct process_control *control, struct task *task);
void process_control_detach_task(struct process_control *control);

/* BSP, IF=0, with an owned reference. Publish once, only after execution
 * resources are reclaimed. Detaches and wakes every waiter. */
void process_control_complete(struct process_control *control, struct process_result result);

/* Retained control object; masks interrupts while sampling completion. */
uint64_t process_control_ready(struct process_control *control);

/* Current process, IF=0, with a live handle reference and checked protocol.
 * WAIT may sleep; one task per process keeps its handle and mappings stable. */
struct syscall_result process_control_call(struct process_control *control,
    uint64_t rights, uint64_t operation, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
