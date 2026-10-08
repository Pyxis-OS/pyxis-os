#ifndef KERNEL_OBJECT_POINTER_H
#define KERNEL_OBJECT_POINTER_H

#include <abi/pointer.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct space;
struct process;
struct task_wait;
struct pointer_image;

#define POINTER_EVENT_CAPACITY 64

struct pointer_object {
  struct kernel_object object;
  struct space *space; /* Spaces outlive their objects. */
  atomic_bool locked;
  struct process *owner; /* Mutated BSP-only, read under lock. */
  struct task_wait *reader;
  struct pointer_event events[POINTER_EVENT_CAPACITY];
  size_t head, count;
  bool focused;
  uint32_t accepted;
  struct pointer_image *image; /* BSP-owned immutable image, one reference. */
  bool hidden;
};

/* Internal image staging never passes a caller's private pointer to BSP. */
enum pointer_request_stage {
  POINTER_REQUEST_COMMAND,
  POINTER_REQUEST_IMAGE_PREPARE,
  POINTER_REQUEST_IMAGE_COMMIT,
};

struct pointer_request {
  struct bsp_request request;
  struct process *process;
  struct pointer_object *pointer;
  uint64_t operation;
  enum pointer_request_stage stage;
  enum call_status result;
  union {
    struct pointer_geometry geometry;
    struct pointer_image_request image;
    struct pointer_warp_request warp;
    uint64_t visible;
  } data;
  struct pointer_image *candidate;
};

/* BSP/IF=0. Space owns initial reference. Queue lock precedes task queue lock. */
struct pointer_object *pointer_create(struct space *space);
void pointer_process_exit(struct process *process);
void pointer_request_execute(struct pointer_request *request);
void pointer_end_session(struct pointer_object *pointer);
void pointer_focus(struct pointer_object *pointer, bool focused);
void pointer_reset_input(struct pointer_object *pointer, uint32_t type);
void pointer_queue_input(struct pointer_object *pointer, struct pointer_event event,
    uint32_t pressed, bool buttons);
void pointer_queue_state(struct pointer_object *pointer, uint32_t type);
/* Current user task/IF=0. READ may block; mutations use BSP requests. */
struct syscall_result pointer_call(struct pointer_object *pointer, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
