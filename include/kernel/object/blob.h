#ifndef KERNEL_BLOB_H
#define KERNEL_BLOB_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct initrd_file;

struct blob_object {
  struct kernel_object object;
  const void *data;
  size_t size;
};

/* BSP, IF=0. file must be an immutable view from initrd_lookup(). Copies the
 * view and returns one owned reference, or NULL on allocation failure. The
 * archive owns the backing for the kernel lifetime; destruction frees only
 * this wrapper, never the bytes, frames or mapping. */
struct blob_object *blob_create(const struct initrd_file *file);

/* Current process, IF=0. Caller holds a live reference and supplies its granted
 * rights. Captures requests and checks every user destination before copying;
 * writes the reply last if it overlaps the data destination. */
struct syscall_result blob_call(struct blob_object *blob, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
