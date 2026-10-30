#ifndef KERNEL_FS_NPFS_H
#define KERNEL_FS_NPFS_H

#include <abi/directory.h>
#include <abi/file.h>
#include <abi/mount.h>
#include <abi/disk.h>
#include <abi/package_commands.h>
#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/gpt.h>
#include <kernel/boot.h>
#include <kernel/service/request.h>
#include <kernel/object/capability.h>
#include <kernel/user/image_capture.h>
#include <pyxis_fs/npfs.h>

#define NPFS_REQUEST_LIMIT 32u
#define NPFS_ADAPTER_BYTES (1024u * 1024u)
#define NPFS_WRAPPER_LIMIT 1024u
/* Package views and the indexes they capture, apart from the wrapper payload:
 * one NPFS_PACKAGE_BUDGET_RAM_DIVISOR-th of installed RAM, at least
 * NPFS_PACKAGE_BUDGET_MIN_BYTES. The memory is real; no count or file size is
 * limited by a constant (docs/interfaces/limits.md). */
#define NPFS_PACKAGE_BUDGET_MIN_BYTES (4u * 1024u * 1024u)
#define NPFS_PACKAGE_BUDGET_RAM_DIVISOR 512u
#define NPFS_TIMEOUT_MS 30000u
#define NPFS_DIRECTORY_RIGHTS DIRECTORY_RIGHTS

_Static_assert(DISK_IO_MAX_BYTES >= FILE_READ_MAX_BYTES &&
    DISK_IO_MAX_BYTES >= FILE_WRITE_MAX_BYTES, "native request transfer capacity");

struct disk_object;
struct npfs_node;
struct fat32_node;
struct process_control;
struct kernel_object;
struct npfs_request;
struct native_completion;
struct file_object;
struct file_transfer_state;
struct bulk_buffer;

enum npfs_operation {
  NPFS_ROOT, NPFS_LOOKUP, NPFS_ENUMERATE, NPFS_READ, NPFS_READ_BULK,
  NPFS_MEMORY_READ_BULK, NPFS_SIZE,
  NPFS_COMMAND_VIEW, NPFS_COMMAND_CAPTURE, NPFS_COMMAND_INFO, NPFS_COMMAND_LOOKUP, NPFS_COMMAND_BIND,
  NPFS_ROOT_VIEW, NPFS_ROOT_SELECT, NPFS_SDK_VIEW,
  NPFS_CAPTURE, NPFS_FILESYSTEM_INFO, NPFS_INFO, NPFS_CREATE, NPFS_REMOVE,
  NPFS_RENAME, NPFS_LINK, NPFS_WRITE, NPFS_APPEND, NPFS_RESIZE, NPFS_SYNC, NPFS_DISK_SYNC, NPFS_CREATE_VOLUME,
  NPFS_RAM_ROOT,
  NPFS_FAT_OPEN, NPFS_FAT_ROOT, NPFS_FAT_STATE, NPFS_FAT_RETIRE,
  NPFS_FAT_SYNC, NPFS_FAT_BEGIN, NPFS_FAT_END, NPFS_FAT_CHECK, NPFS_FAT_FINISH_BEGIN,
  NPFS_RAW_INFO, NPFS_RAW_OPEN, NPFS_RAW_READ, NPFS_RAW_WRITE,
  NPFS_RAW_FLUSH, NPFS_RAW_RELEASE, NPFS_RAW_CLAIM,
  NPFS_RAW_REVIEW, NPFS_RAW_OPEN_PARTITION, NPFS_RAW_PARTITION_INFO,
  NPFS_RAW_BEGIN_ERASE, NPFS_RAW_APPEND_POOL,
};
enum npfs_job_state {
  NPFS_JOB_IDLE, NPFS_JOB_QUEUED, NPFS_JOB_ACTIVE, NPFS_JOB_COMPLETE,
};

/* Shared kernel storage, never user pointers or remote stacks. ROOT's disk
 * is trusted embedding authority, not caller-supplied authentication.
 * Other operations derive children only from the held node and the calling
 * capability's actual rights. Inputs are borrowed until detached completion;
 * successful ROOT/LOOKUP transfers one object reference, failure transfers none.
 * CAPTURE transfers an owned BSP-only image_capture; captured.size is its byte
 * count. Failure leaves it empty; backing is outside the native wrapper cap. FILESYSTEM_INFO
 * copies retained metadata without disk reads. INFO samples current inode data
 * for either kind; directory self-metadata needs no content rights. READ/ENUMERATE publish data
 * only on success. CREATE_VOLUME takes the device, partition, name and a
 * caller-generated volume ID in data. Format/backing diagnostics belong to this
 * operation alone. APPEND selects canonical backing EOF inside the worker;
 * offset and count return the selected start and confirmed byte count. */
struct npfs_job {
  enum npfs_job_state state;
  enum npfs_operation operation;
  struct gpt_guid disk;
  block_device_id device;
  struct disk_object *raw;
  struct disks_review_request disk_review;
  struct disk_partition_selection disk_selection;
  struct disk_append_reply disk_append;
  uint32_t partition;
  struct npfs_node *node, *destination;
  struct fat32_node *fat_node, *fat_destination;
  struct process_control *fat_actor;
  uint64_t fat_access, interval_id, interval_deadline;
  struct boot_origin origin;
  uint64_t rights, child_rights, destination_rights, kind, offset;
  /* LOOKUP with ANY selects child_rights for FILE, this mask for DIRECTORY. */
  uint64_t directory_child_rights;
  size_t count;
  /* Separate bounded bulk storage; held through actual worker retirement.
   * Memory reads also own a retained FILE and its busy loan. */
  struct bulk_buffer *bulk;
  struct file_object *bulk_memory;
  /* FILE_TRANSFER owns both captured objects and its memory loan until actual
   * worker retirement. Ordinary FILE messages leave these fields empty. */
  struct capability_reference transfer_source;
  struct file_object *transfer_destination, *transfer_memory;
  struct file_transfer_state *transfer;
  uint8_t *transfer_data;
  size_t transfer_capacity;
  uint64_t transfer_offset;
  size_t progress; /* Completion profile: known copied/accepted byte prefix. */
  char name[NPFS_NAME_MAX + 1];
  char destination_name[NPFS_NAME_MAX + 1];
  size_t destination_length;
  bool replace, fat_write_authority, fat_owned_esp, interval_delivered;
  /* CREATE claims capacity before submission and prepares logical ownership
   * before native mutation. Completion drains both before returning ownership. */
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  struct capability_grant grant;
  handle_t handle;
  struct directory_cursor cursor;
  struct directory_enumerate_reply entry;
  struct package_command_info command_info;
  struct package_command_record command_target;
  union {
    uint8_t data[DISK_IO_MAX_BYTES];
    struct directory_filesystem_info info;
    struct file_info_reply file_info;
    struct disk_info disk_info;
    struct mount_state_reply mount_state;
  };
  enum call_status status;
  enum npfs_status format_status;
  enum block_result backing_error;
  struct kernel_object *object, *control;
  struct image_capture captured;
  /* Internal queue/completion ownership. */
  struct npfs_job *next;
  struct npfs_request *user_request;
  struct native_completion *completion; /* Owned independent completion record. */
  uint64_t deadline;
  bool admitted;
};

/* One provisioned request per user task. A live capability retains node through
 * the uninterruptible call; no capability entry or user buffer crosses to BSP.
 * CREATE moves its caller-owned slot claim into the request until completion.
 * Consume/detach outputs before release, including on stop. */
struct npfs_request {
  struct bsp_request request;
  struct npfs_job job;
};

struct npfs_request *npfs_request_prepare(enum npfs_operation operation);
void npfs_request_submit_and_wait(struct npfs_request *request);
void npfs_request_release(struct npfs_request *request);
/* Successful BEGIN retains admission through this allocation-free finish. */
void npfs_request_finish_fat32_begin(struct npfs_request *request, bool delivered);
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
/* BSP/IF=0. Owns a separate job and one of the same 32 admission slots before
 * acceptance. Caller fills copied inputs before start; no table crosses over. */
enum call_status npfs_completion_reserve(struct native_completion *operation,
    enum npfs_operation code, struct npfs_node *node);
void npfs_completion_start(struct native_completion *operation);
void npfs_completion_discard(struct native_completion *operation);
/* BSP/IF=0 once after task_init and gpt_start. */
void npfs_start(void);
/* Engine invariant: only the filesystem worker on BSP with IF=1 owns state. */
void npfs_require_worker(void);
/* Sole worker, shared bounded native-wrapper allocation. */
enum call_status npfs_adapter_allocate(size_t bytes, void **out);
void npfs_adapter_free(void *data, size_t bytes);
enum call_status npfs_package_allocate(size_t bytes, void **out);
void npfs_package_free(void *data, size_t bytes);
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
