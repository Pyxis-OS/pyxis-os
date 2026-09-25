#ifndef KERNEL_FS_HOSTFS_H
#define KERNEL_FS_HOSTFS_H

#include <abi/directory.h>
#include <abi/syscall.h>
#include <kernel/virtio/fs.h>

struct kernel_object;
struct capability_table;
struct task_wait;
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
  HOSTFS_SYNC,
  HOSTFS_ENUMERATE,
};

/* One request per calling task, in shared task metadata. No user addresses or
 * private-stack pointers cross the worker boundary. The live capability keeps
 * node alive until completion; a returned object is one owned reference. */
struct hostfs_request {
  struct hostfs_request *next;
  struct task_wait *wait;
  enum hostfs_operation operation;
  struct hostfs_node *node;
  uint64_t kind, offset;
  size_t count;
  /* CAPTURE: count starts as a byte limit. Success transfers an owned heap
   * buffer and sets count to its size; failure leaves captured NULL. The BSP
   * must free it. No capture retry or coherent-host-snapshot guarantee. */
  void *captured;
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

/* BSP, IF=0. Queue caller storage or an object's final destruction, never
 * sleep/allocate. Submit wakes even unavailable requests. Retire transfers the
 * native wrapper and node storage to the worker; the reaper must not free them. */
void hostfs_submit(struct hostfs_request *request);
void hostfs_retire(struct hostfs_node *node);

#endif
