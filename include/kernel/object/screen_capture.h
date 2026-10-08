#ifndef KERNEL_OBJECT_SCREEN_CAPTURE_H
#define KERNEL_OBJECT_SCREEN_CAPTURE_H

#include <abi/screen_capture.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct capability_table;

struct screen_capture_request {
  struct bsp_request request;
  struct capability_table *table; /* Exclusive parked-caller loan until completion. */
  struct screen_capture_reply reply;
  enum call_status status;
};

/* BSP/IF=0, owned reference to observation of the whole local screen. */
struct kernel_object *screen_capture_create(void);
/* Current user syscall, IF=0. Validates output before handing off the caller's
 * capability table; the presenter installs the immutable FILE before wakeup. */
struct syscall_result screen_capture_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity);

#endif
