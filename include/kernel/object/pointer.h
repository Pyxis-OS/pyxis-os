#ifndef KERNEL_OBJECT_POINTER_H
#define KERNEL_OBJECT_POINTER_H

#include <abi/pointer.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct space;
struct process;
struct task_wait;
struct mouse_event;

#define POINTER_EVENT_CAPACITY 64

struct pointer_object {
  struct kernel_object object;
  struct space *space; /* Borrowed; spaces outlive their objects. */
  atomic_bool locked;
  struct process *owner; /* Borrowed under lock, cleared before destruction. */
  struct task_wait *reader; /* Task metadata, never a private-stack pointer. */
  struct pointer_event events[POINTER_EVENT_CAPACITY];
  size_t head, count;
  bool focused;
  uint32_t accepted; /* Held buttons pressed since the last routing reset. */
};

/* BSP, IF=0. Space retains the initial reference. */
struct pointer_object *pointer_create(struct space *space, bool focused);
void pointer_process_exit(struct process *process);
/* BSP routing, IF=0. pressed holds the buttons this device event newly pressed.
 * Lock order: pointer -> task queues. */
void pointer_route_event(struct pointer_object *pointer, const struct mouse_event *event,
                         uint32_t pressed);
void pointer_focus(struct pointer_object *pointer, bool focused);
void pointer_reset_input(struct pointer_object *pointer);
/* Current user task, IF=0. No allocation; READ may block. */
struct syscall_result pointer_call(struct pointer_object *pointer, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
