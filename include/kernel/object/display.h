#ifndef KERNEL_OBJECT_DISPLAY_H
#define KERNEL_OBJECT_DISPLAY_H

#include <abi/display.h>
#include <abi/syscall.h>
#include <kernel/fb/fb.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>
#include <kernel/display_retire.h>

struct process;
struct space;
struct display_object;

enum display_control {
  DISPLAY_CONTROL_DIRECT,
  DISPLAY_CONTROL_RESERVE,
  DISPLAY_CONTROL_COMMIT,
  DISPLAY_CONTROL_PUBLISH,
  DISPLAY_CONTROL_CANCEL,
};

union display_reply {
  struct display_buffer buffer;
  struct display_size_reply size;
  struct display_submit_reply submit;
};

struct display_request {
  struct bsp_request request;
  struct bsp_request vm_request; /* Local transaction; control reuses request above. */
  struct process *loan;
  struct display_object *display; /* Kept alive by the caller's capability. */
  enum display_control control;
  enum call_status control_result;
  uint64_t operation;
  uint64_t generation; /* REPLACE */
  uint64_t slot; /* SUBMIT */
  uintptr_t reply_address; /* Checked user range; REPLACE must not retire it. */
  union display_reply reply;
  enum call_status result;
  struct framebuffer layout;
  uint64_t geometry, mapping_identity;
  uintptr_t previous_address;
  size_t previous_size;
  struct display_frame *slots[DISPLAY_SLOT_COUNT];
  struct display_retirement retirement;
  bool committing;
};

/* BSP, IF=0, after deferred publication. Fixed control phases never mutate the
 * caller's VM; SIZE/SUBMIT preserve the ordinary handoff. */
void display_request_execute(struct display_request *request);
void display_vm_request_execute(struct bsp_request *request);

/* BSP, IF=0: the session owns one reference per slot, plus one for the frame
 * on screen, and each in-flight presentation owns one. User mappings borrow the
 * backing; retirement removes them before dropping the session references. The
 * program may write only the slots it holds, never the pending or current one. */
struct display_frame {
  struct framebuffer fb;
  size_t references;
  struct display_frame *retire_next; /* Detached final reference owned by RAM reaper. */
  struct execution_group *cleanup_group; /* Retired session backing awaiting the presenter. */
  size_t preparation_cpu;
  struct display_retirement *retirement;
};

struct display_object {
  struct kernel_object object;
  struct space *space; /* Borrowed; initialized spaces outlive their objects. */
  struct process *owner; /* BSP-only, cleared before process destruction. */
  struct display_request *transaction; /* BSP-owned reservation, caller retains storage. */
  struct process *retiring_process;
  struct display_frame *slots[DISPLAY_SLOT_COUNT];
  size_t pending; /* Submitted slot not yet presented, or DISPLAY_SLOT_COUNT. */
  /* Frame on screen. It is one of the slots, or after REPLACE an unmapped old
   * slot kept until the next presentation takes a new frame. */
  struct display_frame *current;
  uintptr_t user_address; /* Slot 0; slot i follows at i times the slot size. */
  uint64_t mapping_identity; /* Advances on each acquisition/REPLACE, never reused. */
  uint64_t visual_generation; /* BSP-only; read without taking a pending frame. */
  bool presented; /* First SUBMIT makes the session available as a layer. */
  bool visible; /* User's per-session choice, independent of space selection. */
};

/* BSP, IF=0. The space retains the initial object reference. */
struct display_object *display_create(struct space *space);
/* Current user task, IF=0; VM work uses its kernel-root continuation. */
struct syscall_result display_call(struct display_object *display, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
/* Process exit: fixed prepare, recorded RAM actor unmap, fixed reference drop. */
void display_process_exit_prepare(struct process *process);
void display_process_exit_unmap(struct process *process);
void display_process_exit_finish(struct process *process);
/* BSP, IF=0: snapshot takes any pending frame as current and retains its
 * backing across a preemptible copy. NULL means TTY. */
struct display_frame *display_snapshot(struct display_object *display);
/* BSP, IF=0. The owner's slot layout, or NULL without an acquired session. */
const struct framebuffer *display_slot_layout(const struct display_object *display);
void display_frame_release(struct display_frame *frame);
/* BSP IF=0: reserve geometry publication against an admitted mapping commit. */
bool display_geometry_begin(void);
void display_geometry_end(void);
/* BSP, IF=0. No-op before the first SUBMIT or when the layer is already selected. */
void display_select_layer(struct display_object *display, bool graphics);

#endif
