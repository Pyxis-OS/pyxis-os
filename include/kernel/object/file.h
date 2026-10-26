#ifndef KERNEL_FILE_H
#define KERNEL_FILE_H

#include <abi/syscall.h>
#include <abi/file.h>
#include <abi/describe.h>
#include <kernel/mm/types.h>
#include <kernel/fs/metadata.h>
#include <kernel/object/object.h>
#include <kernel/wait.h>

struct initrd_file;
struct hostfs_node;
struct npfs_node;
struct fat32_node;
struct image_capture;
struct native_completion;
struct retention_account;
struct file_failure;
struct npfs_job;
struct capability_reference;
struct process_control;

enum file_backing {
  FILE_INITRD,
  FILE_RAM,
  FILE_HOST,
  FILE_NPFS,
  FILE_FAT32,
};

/* The short spinlock protects busy and its FIFO. An operation owns busy for its
 * whole duration; all access to size, the RAM page index and its frames requires
 * that ownership. No shared seek position. Immutable boot bytes remain owned by
 * their kernel-lifetime mappings. Native files use only the worker-owned node,
 * never busy or in-memory data.
 *
 * RAM files index one PMM frame per 4 KiB page; a zero entry is a hole that
 * reads as zeros. Frames exist only below the size's last page, and bytes past
 * the size within a frame are always zero, so growth never exposes old data. */
struct file_object {
  struct kernel_object object;
  enum file_backing backing;
  struct fs_metadata metadata; /* RAM/archive, under operation ownership. */
  struct hostfs_node *host; /* Owned by the deferred host worker destructor. */
  struct fat32_node *fat32; /* Worker-owned read-only FAT view. */
  struct npfs_node *npfs; /* Worker owns this wrapper and its core view. */
  const void *data; /* Boot-file bytes only. */
  size_t size;
  phys_addr_t *pages; /* RAM: heap index of page_capacity entries. */
  size_t page_capacity;
  struct retention_account *retention; /* Owns origin account until backing dies. */
  size_t retained_bytes; /* Reserved logical length, under operation ownership. */
  _Atomic(const struct file_failure *) failure; /* NULL or immutable descriptor. */
  bool failure_owned;
  atomic_bool locked;
  atomic_bool immutable_snapshot; /* Sealing is permanent, under operation ownership. */
  bool erase_backing; /* Private RAM snapshots: erase frames before releasing them. */
  bool busy;
  struct task_wait_link *first_waiter, *last_waiter;
};

/* BSP, IF=0. view must borrow immutable kernel-lifetime boot-file storage.
 * Copies the view and returns one owned reference, or NULL on allocation failure.
 * The boot mappings own the backing; destruction frees only
 * this wrapper, never the bytes, frames or mapping. */
struct file_object *file_create_initrd(const struct initrd_file *view);

/* BSP, IF=0. One owned reference to an empty RAM file, or NULL. */
struct file_object *file_create_ram(void);
/* Private retained backing and bounded failed values; account is borrowed, then
 * pinned by the new FILE. These constructors are BSP/IF=0. */
struct file_object *file_create_retained(struct retention_account *account);
struct file_object *file_create_failure(struct retention_account *account,
    uint64_t outcome, const char *detail, size_t detail_size);
enum call_status file_failure_status(const struct file_object *file);
void file_description(struct file_object *file, struct resource_description *description,
    uint64_t *properties);

/* BSP, IF=0. Prepare all backing (including an empty snapshot), then fill once
 * before publishing any READ grant. Fill seals it against WRITE/RESIZE. The
 * caller owns one reference and keeps source data. */
struct file_object *file_prepare_snapshot(size_t size);
/* Same unpublished ownership; erase backing on failure and final destruction. */
struct file_object *file_prepare_secret_snapshot(size_t size);
void file_fill_snapshot(struct file_object *file, const void *data);
/* Unpublished prepared snapshot only; size cannot exceed its reserved length.
 * Shrinks backing, zeros its tail and seals without allocation. */
void file_fill_snapshot_size(struct file_object *file, const void *data, size_t size);

/* BSP, IF=0. Worker supplies stable host state; no in-memory file data. */
struct file_object *file_create_host(struct hostfs_node *host);

/* BSP worker, IF=0. Initialize embedded storage without allocating or supplying
 * in-memory data. Final destruction retires the complete node. */
void file_init_npfs(struct file_object *file, struct npfs_node *node);
void file_init_fat32(struct file_object *file, struct fat32_node *node);

/* Initrd/RAM only, IF=0, with an owned/borrowed live reference. Begin runs on a user task and
 * may sleep; end can run on BSP after a loan. Ownership keeps the contents and
 * size stable without a held spinlock, including while loading an executable
 * from boot bytes. Begin returns false after a stop request, with no queued
 * link or busy ownership. */
bool file_begin_operation(struct file_object *file);
void file_end_operation(struct file_object *file);

/* IF=0, retained file. No operation wait or backend request: a busy RAM file
 * or worker-owned backing leaves length unknown for a description snapshot. */
bool file_cached_length(struct file_object *file, uint64_t *length);

/* BSP, RAM only, IF=0, with operation ownership lent by the parked caller.
 * Copies the selected file into bounded reclaimable capture pages. Caller ends
 * the operation on every result. Failure leaves the capture empty. */
enum call_status file_ram_capture(struct file_object *file, struct image_capture *capture);

/* Current process, IF=0. Caller holds a live reference and supplies its granted
 * rights and operation from a checked protocol tag. request_address/size
 * describe the payload after that tag. Captures requests and checks all user
 * buffers before mutation. May sleep for file ownership; RAM frames and the
 * page index are allocated on the calling CPU;
 * native operations forward actual rights and copied write bytes to the worker,
 * retaining the caller's live reference through completion. Writes the reply
 * last if it overlaps the data destination. */
struct syscall_result file_call(struct file_object *file, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

/* Kernel-held sealed RAM backing, captured request, READ authority. Export
 * admission/lifetime stays with the caller. Copies the small kernel prefix and
 * FILE reply under one mapping lease using the ordinary RAM copy path. */
struct syscall_result file_read_captured(struct file_object *file,
    const struct file_read_request *request, uintptr_t reply_address,
    const void *prefix, size_t prefix_size);

/* Private output backing only. Uses immutable kernel-captured request/data so
 * output mode admission cannot race a second reading of user metadata. Same
 * ownership, allocation and late output rules as ordinary RAM FILE calls. */
struct syscall_result file_output_write(struct file_object *file,
    const struct file_write_request *request, const void *data,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result file_output_resize(struct file_object *file, size_t size);

/* Capture a second FILE grant into the current operation lineage, before a
 * synchronous handoff or independent completion acceptance. */
enum call_status file_transfer_capture(const struct file_transfer_request *request,
    struct capability_reference *source);
/* Job initialization retains destination, moves source and nonblockingly loans
 * memory ownership on success, before acceptance. No I/O or allocations.
 * Worker prepare preallocates
 * every buffer/page before filesystem effects; finish/discard release the loan
 * and both objects on every path, including cancellation and admission failure. */
enum call_status file_transfer_job_init(struct npfs_job *job, struct file_object *destination,
    uint64_t rights, struct capability_reference *source,
    const struct file_transfer_request *request, struct process_control *actor);
enum call_status file_transfer_prepare(struct npfs_job *job);
void file_transfer_finish(struct npfs_job *job);

/* Existing filesystem worker, IF=1. The job owns busy and a live memory FILE.
 * Copies page-sized pieces with short scratch mappings and fairness points. */
enum call_status file_bulk_read_memory(struct npfs_job *job);

/* BSP/IF=0, copied kernel payload and retained target. RAM/archive reserve busy
 * without waiting; native admission reserves the existing filesystem queue. */
enum call_status file_completion_reserve(struct native_completion *operation);
void file_completion_start(struct native_completion *operation);
void file_completion_discard(struct native_completion *operation);

#endif
