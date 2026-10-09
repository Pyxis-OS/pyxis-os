#ifndef KERNEL_FILE_H
#define KERNEL_FILE_H

#include <abi/syscall.h>
#include <kernel/mm/types.h>
#include <kernel/object/object.h>
#include <kernel/wait.h>

struct initrd_file;
struct hostfs_node;
struct npfs_node;
struct image_capture;

enum file_backing {
  FILE_INITRD,
  FILE_RAM,
  FILE_HOST,
  FILE_NPFS,
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
  struct hostfs_node *host; /* Owned by the deferred host worker destructor. */
  struct npfs_node *npfs; /* Worker owns this wrapper and its core view. */
  const void *data; /* Boot-file bytes only. */
  size_t size;
  phys_addr_t *pages; /* RAM: heap index of page_capacity entries. */
  size_t page_capacity;
  atomic_bool locked;
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

/* BSP, IF=0. Copies nonzero size bytes into a new RAM file and returns one owned
 * reference, or NULL on allocation failure. The caller keeps data. Publish only
 * READ grants. */
struct file_object *file_create_snapshot(const void *data, size_t size);

/* BSP, IF=0. Worker supplies stable host state; no in-memory file data. */
struct file_object *file_create_host(struct hostfs_node *host);

/* BSP worker, IF=0. Initialize embedded storage without allocating or supplying
 * in-memory data. Final destruction retires the complete node. */
void file_init_npfs(struct file_object *file, struct npfs_node *node);

/* Initrd/RAM only, IF=0, with an owned/borrowed live reference. Begin runs on a user task and
 * may sleep; end can run on BSP after a loan. Ownership keeps the contents and
 * size stable without a held spinlock, including while loading an executable
 * from boot bytes. Begin returns false after a stop request, with no queued
 * link or busy ownership. */
bool file_begin_operation(struct file_object *file);
void file_end_operation(struct file_object *file);

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

#endif
