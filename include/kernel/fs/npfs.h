#ifndef KERNEL_FS_NPFS_H
#define KERNEL_FS_NPFS_H

#include <abi/directory.h>
#include <abi/file.h>
#include <abi/disk.h>
#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/gpt.h>
#include <kernel/service/request.h>
#include <pyxis_fs/npfs.h>

#define NPFS_REQUEST_LIMIT 32u
#define NPFS_ADAPTER_BYTES (1024u * 1024u)
#define NPFS_WRAPPER_LIMIT 1024u
#define NPFS_TIMEOUT_MS 30000u
#define NPFS_DIRECTORY_RIGHTS DIRECTORY_RIGHTS

_Static_assert(DISK_IO_MAX_BYTES >= FILE_READ_MAX_BYTES &&
    DISK_IO_MAX_BYTES >= FILE_WRITE_MAX_BYTES, "native request transfer capacity");

struct disk_object;
struct npfs_node;
struct kernel_object;
struct npfs_request;
struct capability_table;

enum npfs_operation {
  NPFS_ROOT, NPFS_LOOKUP, NPFS_ENUMERATE, NPFS_READ, NPFS_SIZE,
  NPFS_CAPTURE, NPFS_FILESYSTEM_INFO, NPFS_CREATE, NPFS_REMOVE,
  NPFS_RENAME, NPFS_WRITE, NPFS_RESIZE, NPFS_SYNC, NPFS_DISK_SYNC, NPFS_CREATE_VOLUME,
  NPFS_RAW_INFO, NPFS_RAW_OPEN, NPFS_RAW_READ, NPFS_RAW_WRITE,
  NPFS_RAW_FLUSH, NPFS_RAW_RELEASE, NPFS_RAW_CLAIM,
};
enum npfs_job_state {
  NPFS_JOB_IDLE, NPFS_JOB_QUEUED, NPFS_JOB_ACTIVE, NPFS_JOB_COMPLETE,
};

/* Shared kernel storage, never user pointers or remote stacks. ROOT's disk
 * is trusted embedding authority, not caller-supplied authentication.
 * Other operations derive children only from the held node and the calling
 * capability's actual rights. Inputs are borrowed until detached completion;
 * successful ROOT/LOOKUP transfers one object reference, failure transfers none.
 * CAPTURE transfers owned launch staging (count bytes) on success, none on
 * failure; its allocation is outside the native wrapper cap. FILESYSTEM_INFO
 * copies retained metadata without disk reads. READ/ENUMERATE publish data
 * only on success. CREATE_VOLUME takes the device, partition, name and a
 * caller-generated volume ID in data. Format/backing diagnostics belong to this
 * operation alone. */
struct npfs_job {
  enum npfs_job_state state;
  enum npfs_operation operation;
  struct gpt_guid disk;
  block_device_id device;
  struct disk_object *raw;
  uint32_t partition;
  struct npfs_node *node, *destination;
  uint64_t rights, child_rights, destination_rights, kind, offset;
  size_t count;
  char name[NPFS_NAME_MAX + 1];
  char destination_name[NPFS_NAME_MAX + 1];
  size_t destination_length;
  bool replace;
  struct capability_table *table; /* Exclusive CREATE loan until completion. */
  handle_t handle;
  struct directory_cursor cursor;
  struct directory_enumerate_reply entry;
  union {
    uint8_t data[DISK_IO_MAX_BYTES];
    struct directory_filesystem_info info;
    struct disk_info disk_info;
  };
  enum call_status status;
  enum npfs_status format_status;
  enum block_result backing_error;
  struct kernel_object *object;
  void *captured;
  /* Internal queue/completion ownership. */
  struct npfs_job *next;
  struct npfs_request *user_request;
  uint64_t deadline;
  bool admitted;
};

/* One provisioned request per user task. A live capability retains node through
 * the uninterruptible call; no capability entry or user buffer crosses to BSP.
 * CREATE alone lends the capability table exclusively until completion.
 * Consume/detach outputs before release, including on stop. */
struct npfs_request {
  struct bsp_request request;
  struct npfs_job job;
};

struct npfs_request *npfs_request_prepare(enum npfs_operation operation);
void npfs_request_submit_and_wait(struct npfs_request *request);
void npfs_request_release(struct npfs_request *request);
/* Any publishing CPU, IF=0. Includes time in the executor FIFO and reserves one
 * of the 32 slots there; saturation is completed by the executor with BUSY. */
void npfs_request_published(struct npfs_request *request);
/* BSP/IF=0. Takes FORWARDED ownership; no access after transfer. */
void npfs_forward(struct npfs_request *request);

/* Trusted asynchronous kernel submission, BSP/IF=0. IDLE -> QUEUED on success;
 * rejection preserves the record. Caller owns/retains input references until
 * COMPLETE and can inspect completion only on BSP/IF=0. This does not submit a
 * synchronous executor request from a kernel worker. No automatic mount/probe. */
enum call_status npfs_submit(struct npfs_job *job);
/* BSP/IF=0 once after task_init and gpt_start. */
void npfs_start(void);
/* Engine invariant: only the filesystem worker on BSP with IF=1 owns state. */
void npfs_require_worker(void);
/* BSP/IF=0 object destructor: allocation-free transfer of wrapper, inode reference,
 * backing reference and deferred group cleanup to the owning worker. */
void npfs_retire(struct npfs_node *node);

/* The sole worker serializes physical-device mount registration and raw claims. */
bool npfs_device_mounted(block_device_id device);
bool npfs_partition_mounted(block_device_id device, uint32_t partition);
/* BSP/IF=0: notify the worker of allocation-free deferred raw cleanup. */
void npfs_notify(void);

/* Power-off, from another BSP kernel task with IF=1. After already queued
 * requests, the worker writes each writable pool's dirty data and checkpoints
 * it to an EMPTY journal. On success the pools are sealed: requests that could
 * change a pool or device fail with UNAVAILABLE and background writeback stops.
 * On failure nothing is sealed and the first error is returned. Without a
 * worker there is nothing mounted and the call succeeds. */
enum call_status npfs_shutdown_flush(void);
/* BSP/IF=0, after a successful flush whose power-off then failed: unseal. */
void npfs_shutdown_cancel(void);

#endif
