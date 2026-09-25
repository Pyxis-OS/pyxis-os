#ifndef KERNEL_LAUNCHER_H
#define KERNEL_LAUNCHER_H

#include <abi/launcher.h>
#include <abi/syscall.h>
#include <kernel/user/startup.h>

struct kernel_object;
struct file_object;
struct process;

/* Heap storage shared with BSP, never a remote task stack. Bindings/directory
 * entries initially hold grant indices; BSP replaces them with child handles.
 * Source handles and image are borrowed from the blocked caller's table. */
struct launch_capture {
  struct process_startup startup;
  struct launch_grant *grants;
  size_t grant_count;
  struct file_object *image;
  /* Host backing only: owned stable bytes, freed with capture on the BSP. */
  void *host_image;
  size_t host_image_size;
  size_t used;
  enum call_status error;
  _Alignas(uint64_t) unsigned char data[LAUNCH_CAPTURE_MAX_SIZE];
};

/* BSP, IF=0. Stateless, caller-scoped launch authority; one owned reference. */
struct kernel_object *launcher_create(void);

/* Current user task, IF=0, checked protocol tag. Captures input on caller CPU;
 * BSP owns preparation and allocation. May sleep, with no held spinlocks. */
struct syscall_result launcher_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

/* BSP, IF=0. Caller lends its table and either an in-memory file operation or
 * owned host bytes. Releases the file operation on every path. Prepares reply
 * handle before submission; failure unwinds child resources. Capture and host
 * bytes remain owned by the launch service until it frees both. */
enum call_status launcher_start(struct launch_capture *capture, struct process *parent,
                                 size_t cpu_index, handle_t *result);

#endif
