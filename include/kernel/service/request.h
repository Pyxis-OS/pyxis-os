#ifndef KERNEL_SERVICE_REQUEST_H
#define KERNEL_SERVICE_REQUEST_H

#include <stdbool.h>

struct task_wait;

enum bsp_service {
  BSP_SERVICE_PIPE_CREATE,
};

enum bsp_request_state {
  BSP_REQUEST_FREE,
  BSP_REQUEST_PREPARED,
  BSP_REQUEST_QUEUED,
  BSP_REQUEST_SERVICING,
  BSP_REQUEST_COMPLETE,
};

/* Shared task-lifetime storage, never a remote stack pointer. The caller owns
 * FREE/PREPARED and completed results after waiting; the BSP owns published
 * requests. Queue publication and wait notification synchronize those loans.
 * COMPLETE is read after notification, never polled as an asynchronous result. */
struct bsp_request {
  struct bsp_request *next;
  struct task_wait *wait;
  enum bsp_service service;
  enum bsp_request_state state;
};

/* Current user task, IF=0, no held locks. One operation through consumption:
 * prepare, fill the typed record, submit/wait, consume results, release.
 * Preparation uses reserved shared storage and cannot fail allocation. */
struct bsp_request *bsp_request_prepare(enum bsp_service service);
void bsp_request_submit_and_wait(struct bsp_request *request);
void bsp_request_release(struct bsp_request *request);

/* Temporary scheduler adapters. BSP, IF=0; drain a detached FIFO batch without
 * holding the queue lock across service. Pending may also run in timer entry.
 * Worker notification and yielding replace these adapters in the next task. */
void bsp_requests_service(void);
bool bsp_requests_pending(void);

#endif
