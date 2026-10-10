#ifndef KERNEL_FS_HOSTFS_H
#define KERNEL_FS_HOSTFS_H

#include <abi/directory.h>
#include <abi/file_info.h>
#include <abi/syscall.h>
#include <kernel/service/request.h>
#include <kernel/user/image_capture.h>
#include <kernel/virtio/fs.h>

struct kernel_object;
struct capability_table;
struct hostfs_node;

enum hostfs_operation {
  HOSTFS_ROOT,
  HOSTFS_LOOKUP,
  HOSTFS_CREATE,
  HOSTFS_REMOVE,
  HOSTFS_RENAME,
  HOSTFS_READ,
  HOSTFS_CAPTURE,
  HOSTFS_WRITE,
  HOSTFS_RESIZE,
  HOSTFS_SIZE,
  HOSTFS_INFO,
  HOSTFS_SYNC,
  HOSTFS_ENUMERATE,
};

struct hostfs_profile {
  bool active;
  size_t requested_bytes;
  uint64_t started_ns, published_ns, forwarded_ns, service_started_ns, service_ended_ns;
  struct virtio_fs_profile transport;
};

/* One request per calling task, in its shared request area. No user addresses or
 * private-stack pointers cross the worker boundary. The live capability keeps
 * node alive until completion; a returned object is one owned reference. Keep
 * the common reservation through result consumption, then explicitly release. */
struct hostfs_request {
  struct bsp_request request;
  struct hostfs_request *next;
  struct hostfs_profile profile;
  enum hostfs_operation operation;
  struct hostfs_node *node;
  uint64_t kind, offset;
  struct file_info_reply info;
  size_t count;
  /* CAPTURE transfers complete BSP-only page backing; failure leaves it empty.
   * No capture retry or coherent-host-snapshot guarantee. The caller moves the
   * descriptor to launch storage and clears it before releasing this request. */
  struct image_capture captured;
  struct directory_cursor cursor;
  char name[VIRTIO_FS_NAME_MAX + 1];
  /* RENAME borrows both parent nodes from the parked caller's capabilities. */
  struct hostfs_node *destination;
  char destination_name[VIRTIO_FS_NAME_MAX + 1];
  size_t destination_length;
  bool replace;
  enum call_status status;
  struct kernel_object *object;
  /* CREATE exclusively lends the blocked caller's table to the BSP worker.
   * Install the result before host mutation; no fallible local work follows. */
  struct capability_table *table;
  uint64_t rights;
  handle_t handle;
  struct directory_enumerate_reply entry;
  uint8_t data[VIRTIO_FS_READ_MAX > VIRTIO_FS_WRITE_MAX ? VIRTIO_FS_READ_MAX : VIRTIO_FS_WRITE_MAX];
};

/* Current user task, IF=0, no held locks. Fill shared staging, submit/wait,
 * consume or detach owned outputs, then release. Only the caller copies user
 * memory. Group stop preserves this uninterruptible handoff through result
 * collection and release before the caller can retire. */
struct hostfs_request *hostfs_request_prepare(enum hostfs_operation operation);
void hostfs_request_submit_and_wait(struct hostfs_request *request);
void hostfs_request_release(struct hostfs_request *request);

/* Common publication hook, IF=0, immediately before the FIFO lock. */
void hostfs_request_published(struct hostfs_request *request);

/* BSP/IF=0 before the worker can run. Submissions wait through bounded INIT.
 * Every worker-creation/initialization failure must complete queued callers. */
void hostfs_prepare(void);
/* BSP, either interrupt state; only before successful start. */
void hostfs_start_failed(enum virtio_fs_result result);

/* Sole transport worker, IF=1. Start publishes the boot-lifetime session.
 * Service performs one queued request or deferred destruction, and may sleep.
 * It must keep running after session failure to retire local objects. */
void hostfs_start(struct virtio_fs_session *session);
bool hostfs_service(void);

/* BSP, IF=0. Submit forwards an executor-owned request to the HOST worker,
 * completing unavailable requests immediately. No request accesses afterward.
 * Retire transfers the native wrapper and node storage to the worker; the reaper
 * must not free them. Retirement carries pending group cleanup through CLOSE,
 * node put and local storage reclamation. Neither operation sleeps or allocates. */
void hostfs_submit(struct hostfs_request *request);
void hostfs_retire(struct hostfs_node *node);

#endif
