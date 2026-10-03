#ifndef KERNEL_FILE_H
#define KERNEL_FILE_H

#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>
#include <kernel/wait.h>

struct initrd_file;
struct hostfs_node;
struct npfs_node;

enum file_backing {
  FILE_INITRD,
  FILE_RAM,
  FILE_HOST,
  FILE_NPFS,
};

/* The short spinlock protects busy and its FIFO. An operation owns busy across
 * BSP allocation waits; all access to data/size/capacity requires that ownership.
 * No shared seek position. Initrd bytes remain owned by the archive. Native
 * files use only the worker-owned node, never busy or in-memory data. */
struct file_object {
  struct kernel_object object;
  enum file_backing backing;
  struct hostfs_node *host; /* Owned by the deferred host worker destructor. */
  struct npfs_node *npfs; /* Worker owns this wrapper and its core view. */
  const void *data;
  size_t size, capacity;
  atomic_bool locked;
  bool busy;
  struct task_wait_link *first_waiter, *last_waiter;
};

/* BSP, IF=0. view must be an immutable view from initrd_lookup(). Copies the
 * view and returns one owned reference, or NULL on allocation failure. The
 * archive owns the backing for the kernel lifetime; destruction frees only
 * this wrapper, never the bytes, frames or mapping. */
struct file_object *file_create_initrd(const struct initrd_file *view);

/* BSP, IF=0. One owned reference to an empty RAM file, or NULL. */
struct file_object *file_create_ram(void);

/* BSP, IF=0. Worker supplies stable host state; no in-memory file data. */
struct file_object *file_create_host(struct hostfs_node *host);

/* BSP worker, IF=0. Initialize embedded storage without allocating or supplying
 * in-memory data. Final destruction retires the complete node. */
void file_init_npfs(struct file_object *file, struct npfs_node *node);

/* Initrd/RAM only, IF=0, with an owned/borrowed live reference. Begin runs on a user task and
 * may sleep; end can run on BSP after a loan. Ownership keeps data/size stable
 * without a held spinlock, including while loading an executable. Begin
 * returns false after a stop request, with no queued link or busy ownership. */
bool file_begin_operation(struct file_object *file);
void file_end_operation(struct file_object *file);

struct file_buffer_profile {
  uint64_t allocation_started, allocation_ended;
  uint64_t copy_started, copy_ended;
  uint64_t release_started, release_ended;
  size_t copied_bytes;
};

struct file_replace_profile {
  bool active;
  uint64_t started_ns, published_ns;
  uint64_t service_started_ns, service_ended_ns;
  struct file_buffer_profile buffer;
};

/* Requester lends exclusive operation ownership while blocked. Capacity must
 * cover the live prefix, or be zero to release the buffer. Failure leaves the
 * old buffer/capacity intact. The caller retains responsibility for logical
 * size and consumes result only after waiting. */
struct file_replace_request {
  struct bsp_request request;
  struct file_object *file;
  size_t capacity;
  bool result;
  struct file_replace_profile profile;
};

/* IF=0, immediately before publication locking while caller-owned/PREPARED. */
void file_replace_published(struct file_replace_request *request);
/* BSP, IF=0, SERVICING. Clears the file loan before completion. */
void file_replace_execute(struct file_replace_request *request);

/* Current process, IF=0. Caller holds a live reference and supplies its granted
 * rights and operation from a checked protocol tag. request_address/size
 * describe the payload after that tag. Captures requests and checks all user
 * buffers before mutation. May sleep for file ownership or BSP allocation;
 * native operations forward actual rights and copied write bytes to the worker,
 * retaining the caller's live reference through completion. Writes the reply
 * last if it overlaps the data destination. */
struct syscall_result file_call(struct file_object *file, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
