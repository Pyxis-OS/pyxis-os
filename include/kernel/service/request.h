#ifndef KERNEL_SERVICE_REQUEST_H
#define KERNEL_SERVICE_REQUEST_H

struct task_wait;
struct execution_group;
struct process;
struct task;
struct forwarder_operation;

enum bsp_service {
  BSP_SERVICE_FORWARDER,
  BSP_SERVICE_PIPE_CREATE,
  BSP_SERVICE_OUTPUT,
  BSP_SERVICE_RETENTION,
  BSP_SERVICE_TERMINAL_CREATE,
  BSP_SERVICE_DISPLAY,
  BSP_SERVICE_MEMORY, /* Preallocated typed storage only; no BSP submission. */
  BSP_SERVICE_POINTER,
  BSP_SERVICE_AUDIO,
  BSP_SERVICE_CAPABILITY_GROW,
  BSP_SERVICE_ENVIRONMENT,
  BSP_SERVICE_ENDPOINT_CREATE,
  BSP_SERVICE_ENDPOINT_EXPORT,
  BSP_SERVICE_ENDPOINT_DESCRIBE,
  BSP_SERVICE_RAMFS,
  BSP_SERVICE_LAUNCHER,
  BSP_SERVICE_HOSTFS,
  BSP_SERVICE_NPFS,
  BSP_SERVICE_READINESS,
  BSP_SERVICE_READINESS_SET,
  BSP_SERVICE_SYSTEM_INFO_MEMORY,
  BSP_SERVICE_SYSTEM_INFO_POWER,
  BSP_SERVICE_SYSTEM_INFO_HOSTNAME,
  BSP_SERVICE_POWER,
  BSP_SERVICE_ACPI_TABLES,
  BSP_SERVICE_SCREEN_CAPTURE,
  BSP_SERVICE_SCREENSHOT_SAVER,
  BSP_SERVICE_BLUETOOTH_HCI,
  BSP_SERVICE_CLIPBOARD,
  BSP_SERVICE_THREAD,
  BSP_SERVICE_PROCESS_INTERRUPT_CREATE,
  BSP_SERVICE_INPUT_POLICY,
  BSP_SERVICE_COMPLETION,
  BSP_SERVICE_NET_FACTORY,
  BSP_SERVICE_NET_CONTROL,
  BSP_SERVICE_WIFI,
  BSP_SERVICE_WIFI_CONNECTION,
  BSP_SERVICE_WIFI_AUTH,
  BSP_SERVICE_COUNT,
};

enum bsp_request_state {
  BSP_REQUEST_FREE,
  BSP_REQUEST_PREPARED,
  BSP_REQUEST_DEFERRED,
  BSP_REQUEST_QUEUED,
  BSP_REQUEST_SERVICING,
  BSP_REQUEST_FORWARDED,
  BSP_REQUEST_COMPLETE,
};

/* Shared task-lifetime storage, never a remote stack pointer. The caller owns
 * FREE/PREPARED and completed results after waiting; the BSP owns published
 * requests. DEFERRED belongs to the caller until its scheduler establishes the
 * parked handoff. Queue publication and wait notification synchronize loans.
 * FORWARDED belongs to the HOST, native filesystem, readiness, audio or ACPI worker,
 * the USB worker, or the sole screen presenter until final completion.
 * MEMORY uses the same retained typed storage locally, without queue publication
 * or a completion notification; its kernel-root caller consumes the result.
 * COMPLETE is read after notification, never polled as an asynchronous result. */
struct bsp_request {
  struct bsp_request *next;
  struct task_wait *wait;
  /* Borrowed while the uninterruptible caller still owns its member reference. */
  struct execution_group *cleanup_group;
  /* Borrowed while the originating operation's caller remains parked. */
  struct forwarder_operation *operation;
  enum bsp_service service;
  enum bsp_request_state state;
  struct process *vm_process; /* Staged local MEMORY or deferred BSP VM mutation. */
  struct task *vm_mutator;
  bool vm_reply_owned; /* Consumed before releasing the completed record. */
};

/* BSP, IF=0. Provision the catalog's largest typed record before publishing a
 * user task. Returns NULL on exhaustion; submission never allocates storage.
 * Destroy only after all requests are released. Kernel workers do not provision. */
struct bsp_request *bsp_request_storage_create(void);
void bsp_request_storage_destroy(struct bsp_request *storage);

/* Current user task, IF=0, no held locks. One operation through consumption:
 * prepare, fill the typed record, submit/wait, consume results, release.
 * Preparation uses reserved shared storage and cannot fail allocation. */
struct bsp_request *bsp_request_prepare(enum bsp_service service);
/* Display and screen capture require post-switch publication, chosen
 * by the closed service catalog. Other requests publish before sleeping. */
void bsp_request_submit_and_wait(struct bsp_request *request);
void bsp_request_release(struct bsp_request *request);
/* Unpublished PREPARED unwind only. Caller first releases all subsystem-owned
 * grants, claims and inputs. Detaches the unused wait; never notifies it. */
void bsp_request_cancel_prepared(struct bsp_request *request);

/* BSP, IF=0. Executor finishes SERVICING; worker completion paths finish FORWARDED.
 * Detach all subsystem/worker references and publish results first. Notification
 * transfers ownership immediately; no request accesses may follow this call. */
void bsp_request_complete(struct bsp_request *request);

/* Scheduler only, IF=0, after parking outside the task stack/private root and
 * clearing current/entry state. No request or caller accesses after publication. */
void bsp_request_publish_deferred(struct bsp_request *request);

/* BSP, IF=0, once after task_init() and before publishing request producers.
 * Failure is fatal: the executor is required infrastructure. Kernel tasks,
 * including the executor, cannot use the synchronous client transport. */
void bsp_requests_init(void);

#endif
