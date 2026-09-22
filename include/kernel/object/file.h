#ifndef KERNEL_FILE_H
#define KERNEL_FILE_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct initrd_file;

/* Initrd is the only backing currently supported. Reads use explicit offsets;
 * neither the object nor its capabilities carry a seek position. */
struct file_object {
  struct kernel_object object;
  const void *data;
  size_t size;
};

/* BSP, IF=0. view must be an immutable view from initrd_lookup(). Copies the
 * view and returns one owned reference, or NULL on allocation failure. The
 * archive owns the backing for the kernel lifetime; destruction frees only
 * this wrapper, never the bytes, frames or mapping. */
struct file_object *file_create_initrd(const struct initrd_file *view);

/* Current process, IF=0. Caller holds a live reference and supplies its granted
 * rights and operation from a checked protocol tag. request_address/size
 * describe the payload after that tag. Captures requests and checks every
 * user destination before copying;
 * writes the reply last if it overlaps the data destination. */
struct syscall_result file_call(struct file_object *file, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
