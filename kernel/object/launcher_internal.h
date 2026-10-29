#ifndef KERNEL_LAUNCHER_INTERNAL_H
#define KERNEL_LAUNCHER_INTERNAL_H

#include <abi/space.h>
#include <kernel/object/launcher.h>
#include <kernel/object/capability.h>
#include <kernel/user/startup.h>
#include <kernel/user/image_capture.h>

struct file_object;
struct space;

/* Space creation only. Text is NUL-terminated and validated by the caller;
 * cpus points into the capture's data and covers cpu_count boot CPUs. */
struct launch_space {
  char name[SPACE_NAME_MAX + 1];
  char title[SPACE_TITLE_MAX + 1];
  char reason[SPACE_REASON_MAX + 1]; /* Empty when a child is launched. */
  const uint64_t *cpus;
  size_t cpu_count;
  bool terminal_control, clipboard_local, clipboard_shared, boot_focus, screenshot_saver;
  bool jit;
};

/* Heap storage shared with BSP, never a remote task stack. Capability-valued
 * environment entries and cwd handles initially hold grant indices.
 * The preparation actor replaces them with child handles.
 * Source grants own logical authority before image capture can wait. The capture
 * owns image storage independently of its optional file operation. */
struct launch_capture {
  struct process_startup startup;
  struct launch_grant *grants;
  size_t grant_count;
  struct capability_grant *owned_grants;
  struct capability_reserved_slot *child_slots;
  handle_t *child_handles;
  size_t initial_stack_bytes; /* Validated, with the default resolved. */
  struct file_object *image; /* Owned storage reference until capture discard. */
  struct image_capture captured_image;
  size_t used;
  enum call_status error;
  struct launch_space space;
  struct space *target_space; /* Boot-lifetime fixed-owner setup result. */
  struct process *prepared_child; /* Lent only through SPACE_DEVICES completion. */
  struct process_environment_entry *space_entries; /* Owned device startup additions. */
  enum call_status space_status;
  _Alignas(uint64_t) unsigned char data[LAUNCH_CAPTURE_MAX_SIZE];
};

/* Retained caller kernel root, IF=0. Transfer a reservation and stable image
 * operation/owned snapshot to exclusive preparation. File read ownership ends
 * before publication; failure synchronously unwinds the local single child. */
enum call_status launcher_start(struct launch_capture *capture, struct process *parent,
    size_t parent_cpu, struct execution_group *execution_group,
    struct capability_reservation *reservation, struct capability_reserved_slot *slots,
    handle_t *result);
/* Fixed BSP device provisioning borrows the inactive child while its preparation
 * actor waits. All shared inputs/results live in the owned heap capture. */
enum call_status launcher_install_space_devices(struct process *child, struct space *space,
    struct process_startup *startup, struct process_environment_entry **owned);
enum call_status launcher_request_space_devices(struct launch_capture *capture,
    struct process *child, struct process_startup *startup,
    struct process_environment_entry **entries);
enum call_status launcher_prepare_space(struct launch_capture *capture,
    struct process *parent, struct capability_reservation *reservation,
    struct capability_reserved_slot *slots, handle_t *result);

/* Current user task, IF=0. Caller-side capture for space creation. capture_request
 * resolves the image and captures the request like LAUNCHER_LAUNCH; capture_empty
 * allocates storage for a space without a child. capture_array copies COUNT
 * items into the capture's data, or returns NULL and records the error.
 * submit_space retains fixed space/device setup around caller-owned preparation
 * and consumes the capture. */
enum call_status launcher_capture_request(const struct launch_request *request,
    struct launch_capture **result);
struct launch_capture *launcher_capture_empty(void);
void *launcher_capture_array(struct launch_capture *capture, uintptr_t address,
    size_t count, size_t item_size);
enum call_status launcher_submit_space(struct launch_capture *capture, handle_t *child);
void launcher_capture_discard(struct launch_capture *capture);

/* Retained caller preparation. A group owns prepared processes and task stacks until
 * publication; observers stay private until publication. Abort releases all
 * reserved slots, observer grants and child grants. */
struct launch_preparation *launcher_batch_create(void);
/* Current user task, before preparing another BSP request. */
enum capability_result launcher_batch_reserve(struct launch_preparation *group, size_t count);
enum call_status launcher_batch_prepare(struct launch_preparation *group,
    struct launch_capture *capture, struct process *parent, size_t parent_cpu,
    struct execution_group *execution_group);
enum call_status launcher_batch_publish(struct launch_preparation *group, handle_t *children);
void launcher_batch_discard(struct launch_preparation *group);

#endif
