#ifndef KERNEL_OBJECT_SCREENSHOT_SAVER_H
#define KERNEL_OBJECT_SCREENSHOT_SAVER_H

#include <abi/screenshot_saver.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct space;
struct process;
struct screenshot_saver;

struct screenshot_saver_request {
  struct bsp_request request;
  struct screenshot_saver *saver; /* Borrowed from the admitted call. */
  uint64_t operation, id;
  enum call_status status;
  uint64_t completion;
  char filename[SCREENSHOT_SAVER_FILENAME_MAX + 1];
  struct screenshot_saver_pending pending;
};

/* BSP/IF=0. Space keeps a weak registration until final storage retirement. */
struct kernel_object *screenshot_saver_create(struct space *space);
/* IF=0, any CPU. Counts logical grants independently of readiness storage. */
bool screenshot_saver_grant_retain(struct kernel_object *object, uint64_t rights);
void screenshot_saver_grant_release(struct kernel_object *object, uint64_t rights);
bool screenshot_saver_in_space(const struct kernel_object *object, const struct space *space);
/* BSP, preserves IF. State is never read remotely by readiness or presenter. */
uint64_t screenshot_saver_ready(struct kernel_object *object);
/* BSP/IF=0. Physical press, before application keyboard routing. */
void screenshot_saver_key(struct space *space);
/* BSP/IF=0 upkeep; handles final SERVE loss and the request policy deadline. */
void screenshot_saver_update(void);
/* BSP/IF=0, before snapshot allocation. On success consumes the capture attempt. */
bool screenshot_saver_capture_begin(struct screenshot_saver *saver, uint64_t id);
bool screenshot_saver_capture_selected(struct screenshot_saver *saver, uint64_t id,
    const struct space *space, uint64_t selection);
void screenshot_saver_capture_finished(struct screenshot_saver *saver, uint64_t id, bool success);
void screenshot_saver_execute(struct screenshot_saver_request *request);
struct syscall_result screenshot_saver_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t address, size_t bytes,
    uintptr_t reply_address, size_t reply_capacity);

#endif
