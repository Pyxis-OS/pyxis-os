#ifndef KERNEL_OBJECT_DISPLAY_H
#define KERNEL_OBJECT_DISPLAY_H

#include <abi/display.h>
#include <abi/syscall.h>
#include <kernel/fb/fb.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct process;
struct space;
struct display_object;

struct display_request {
  struct bsp_request request;
  struct process *loan;
  struct display_object *display; /* Kept alive by the caller's capability. */
  uint64_t operation;
  struct display_buffer reply;
  enum call_status result;
};

/* BSP, IF=0, after deferred publication lends the inactive private space.
 * Every operation, including PRESENT, uses this same handoff. */
void display_request_execute(struct display_request *request);

/* BSP, IF=0: the session and each in-flight presentation own one reference.
 * User mappings borrow the backing; release removes them before dropping the
 * session reference. Pixel contents remain writable concurrently by userspace. */
struct display_frame {
  struct framebuffer fb;
  size_t references;
};

struct display_object {
  struct kernel_object object;
  struct space *space; /* Borrowed; initialized spaces outlive their objects. */
  struct process *owner; /* BSP-only, cleared before process destruction. */
  struct display_frame *frame;
  uintptr_t user_address;
  bool visible;
};

/* BSP, IF=0. The space retains the initial object reference. */
struct display_object *display_create(struct space *space);
/* Current user task, IF=0; validated operations hand off to the BSP. */
struct syscall_result display_call(struct display_object *display, uint64_t rights,
    uint64_t operation, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity);
/* BSP, IF=0, owns the retired process's inactive address space. */
void display_process_exit(struct process *process);
/* BSP, IF=0: snapshot retains backing across a preemptible copy. NULL means TTY. */
struct display_frame *display_snapshot(struct display_object *display);
void display_frame_release(struct display_frame *frame);

#endif
