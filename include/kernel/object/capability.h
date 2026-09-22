#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <stddef.h>
#include <abi/handle.h>

struct kernel_object;
struct capability_entry;

/* Kernel results, not the syscall status encoding. */
enum capability_result {
  CAP_OK,
  CAP_BAD_HANDLE,
  CAP_DENIED,
  CAP_INVALID,
  CAP_NO_MEMORY,
  CAP_LIMIT,
  CAP_FULL,
};

/* Zero initialization creates an empty table. It has one exclusive owner:
 * the BSP before submission/after retirement, otherwise the executing user
 * task with IF=0. During a growth or launch request that task lends the table
 * to the BSP until completion. No concurrent lookup, close, install or teardown. */
struct capability_table {
  struct capability_entry *entries;
  size_t capacity;
};

/* BSP, IF=0, exclusively owning an unsubmitted or caller-lent table.
 * Adds a reference; the caller retains
 * its original one. Rights are an explicit kernel grant, not derived from
 * another handle, and must use the target object's protocol-specific mask.
 * Unsupported bits are rejected. Failure clears *handle and leaves references
 * unchanged. */
enum capability_result capability_install(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, handle_t *handle);

/* IF=0, exclusive table ownership on any CPU. Adds a reference without
 * allocating; CAP_FULL leaves the message/owner free to request BSP growth.
 * Other results and ownership match capability_install(). */
enum capability_result capability_insert(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, handle_t *handle);

/* BSP, IF=0, exclusive ownership (including growth/launch loans).
 * Preserves entries, generations and references; failure leaves them intact. */
enum capability_result capability_grow(struct capability_table *table);

/* BSP, IF=0, exclusive ownership of both tables: unsubmitted processes or
 * a blocked launcher caller lending its source table. Copies with equal or
 * reduced rights; source remains valid. Failure clears
 * the result handle and changes neither table's entries nor object references. */
enum capability_result capability_grant(struct capability_table *destination,
    struct capability_table *source, handle_t handle, uint64_t rights,
    handle_t *result);

/* IF=0, exclusive table ownership (including a BSP loan). Resolve returns a borrowed object, valid only until
 * that entry closes or the table is destroyed; failure clears *object.
 * All required rights must be present. Optional rights receives the granted
 * mask on success, zero on failure. A handle has meaning only in its table. */
enum capability_result capability_resolve(struct capability_table *table,
    handle_t handle, uint64_t required_rights, struct kernel_object **object,
    uint64_t *rights);

/* IF=0, exclusive table ownership; no allocation or destruction. Immediately makes
 * the handle stale and releases its reference through BSP retirement. */
enum capability_result capability_close(struct capability_table *table,
                                         handle_t handle);

/* BSP, IF=0, with exclusive ownership. Releases all entries and table storage.
 * Object destruction is deferred to object_reap(), including on this path. */
void capability_table_destroy(struct capability_table *table);

#endif
