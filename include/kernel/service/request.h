#ifndef KERNEL_SERVICE_REQUEST_H
#define KERNEL_SERVICE_REQUEST_H

struct task_wait;

enum bsp_service {
  BSP_SERVICE_PIPE_CREATE,
  BSP_SERVICE_MEMORY,
  BSP_SERVICE_DISPLAY,
  BSP_SERVICE_CAPABILITY_GROW,
  BSP_SERVICE_NAMESPACE_CREATE,
  BSP_SERVICE_ENDPOINT_CREATE,
  BSP_SERVICE_ENDPOINT_EXPORT,
  BSP_SERVICE_RAMFS,
  BSP_SERVICE_FILE_REPLACE,
};

enum bsp_request_state {
  BSP_REQUEST_FREE,
  BSP_REQUEST_PREPARED,
  BSP_REQUEST_DEFERRED,
  BSP_REQUEST_QUEUED,
  BSP_REQUEST_SERVICING,
  BSP_REQUEST_COMPLETE,
};

/* Shared task-lifetime storage, never a remote stack pointer. The caller owns
 * FREE/PREPARED and completed results after waiting; the BSP owns published
 * requests. DEFERRED belongs to the caller until its scheduler establishes the
 * parked handoff. Queue publication and wait notification synchronize loans.
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
/* Memory and every display operation require post-switch publication, chosen
 * by the closed service catalog. Other requests publish before sleeping. */
void bsp_request_submit_and_wait(struct bsp_request *request);
void bsp_request_release(struct bsp_request *request);

/* Scheduler only, IF=0, after parking outside the task stack/private root and
 * clearing current/entry state. No request or caller accesses after publication. */
void bsp_request_publish_deferred(struct bsp_request *request);

/* BSP, IF=0, once after task_init() and before publishing request producers.
 * Failure is fatal: the executor is required infrastructure. Kernel tasks,
 * including the executor, cannot use the synchronous client transport. */
void bsp_requests_init(void);

#endif
