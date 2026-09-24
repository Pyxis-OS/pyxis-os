#ifndef KERNEL_FS_HOSTFS_H
#define KERNEL_FS_HOSTFS_H

#include <abi/directory.h>
#include <abi/syscall.h>
#include <kernel/virtio/fs.h>

struct kernel_object;
struct task_wait;
struct hostfs_node;

enum hostfs_operation {
  HOSTFS_ROOT,
  HOSTFS_LOOKUP,
  HOSTFS_READ,
  HOSTFS_SIZE,
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
  struct directory_cursor cursor;
  char name[VIRTIO_FS_NAME_MAX + 1];
  enum call_status status;
  struct kernel_object *object;
  struct directory_enumerate_reply entry;
  uint8_t data[VIRTIO_FS_READ_MAX];
};

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
