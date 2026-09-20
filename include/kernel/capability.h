#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <stddef.h>
#include <abi/handle.h>

struct kernel_object;
struct capability_entry;

#define CAP_READ (UINT64_C(1) << 0)
#define CAP_WRITE (UINT64_C(1) << 1)

/* Kernel results, not the syscall status encoding. */
enum capability_result {
  CAP_OK,
  CAP_BAD_HANDLE,
  CAP_DENIED,
  CAP_INVALID,
  CAP_NO_MEMORY,
  CAP_LIMIT,
};

/* Zero initialization creates an empty table. It has one exclusive owner:
 * the BSP before submission/after retirement, otherwise the executing user
 * task with IF=0. No concurrent lookup, close, install or teardown. */
struct capability_table {
  struct capability_entry *entries;
  size_t capacity;
};

/* BSP, IF=0, before process submission. Adds a reference; the caller retains
 * its original one. Rights are an explicit kernel grant, not derived from
 * another handle. Failure clears *handle and leaves references unchanged. */
enum capability_result capability_install(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, handle_t *handle);

/* IF=0 on the owning CPU. Resolve returns a borrowed object, valid only until
 * that entry closes or the table is destroyed; failure clears *object.
 * All required rights must be present. A handle has meaning only in its table. */
enum capability_result capability_resolve(struct capability_table *table,
    handle_t handle, uint64_t required_rights, struct kernel_object **object);

/* IF=0 on the owning CPU; no allocation or destruction. Immediately makes
 * the handle stale and releases its reference through BSP retirement. */
enum capability_result capability_close(struct capability_table *table,
                                         handle_t handle);

/* BSP, IF=0, with exclusive ownership. Releases all entries and table storage.
 * Object destruction is deferred to object_reap(), including on this path. */
void capability_table_destroy(struct capability_table *table);

#endif
