#ifndef KERNEL_FILE_H
#define KERNEL_FILE_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct initrd_file;
struct hostfs_node;

enum file_backing {
  FILE_INITRD,
  FILE_RAM,
  FILE_HOST,
};

struct task_wait;

/* Embedded in task metadata, not a private stack: other CPUs detach and wake it. */
struct file_wait {
  struct file_wait *next;
  struct task_wait *wait;
};

/* The short spinlock protects busy and its FIFO. An operation owns busy across
 * BSP allocation waits; all access to data/size/capacity requires that ownership.
 * No shared seek position. Initrd bytes remain owned by the archive. */
struct file_object {
  struct kernel_object object;
  enum file_backing backing;
  struct hostfs_node *host; /* Owned by the deferred host worker destructor. */
  const void *data;
  size_t size, capacity;
  atomic_bool locked;
  bool busy;
  struct file_wait *first_waiter, *last_waiter;
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

/* IF=0, with an owned/borrowed live reference. Begin runs on a user task and
 * may sleep; end can run on BSP after a loan. Ownership keeps data/size stable
 * without a held spinlock, including while loading an executable. */
void file_begin_operation(struct file_object *file);
void file_end_operation(struct file_object *file);

/* BSP, IF=0. Requester lends exclusive operation ownership while blocked.
 * Capacity must cover the live prefix, or be zero to release the buffer.
 * Failure leaves the old buffer/capacity intact. Does not change logical size. */
struct file_buffer_profile {
  uint64_t allocation_started, allocation_ended;
  uint64_t copy_started, copy_ended;
  uint64_t release_started, release_ended;
  size_t copied_bytes;
};

/* Optional BSP-owned result storage, initialized by the requester. */
bool file_replace_buffer(struct file_object *file, size_t capacity,
    struct file_buffer_profile *profile);

/* Current process, IF=0. Caller holds a live reference and supplies its granted
 * rights and operation from a checked protocol tag. request_address/size
 * describe the payload after that tag. Captures requests and checks all user
 * buffers before mutation. May sleep for file ownership or BSP allocation;
 * writes the reply last if it overlaps the data destination. */
struct syscall_result file_call(struct file_object *file, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
