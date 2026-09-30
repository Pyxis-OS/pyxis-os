#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <stddef.h>
#include <abi/handle.h>
#include <kernel/service/request.h>

struct kernel_object;
struct capability_entry;
struct execution_group;

#define CAPABILITY_BATCH_MAX 5

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
 * task with IF=0. During a growth, launch, network-open or host-create request the task lends
 * the table to the BSP until completion. No concurrent lookup, close, install or teardown. */
struct capability_table {
  struct capability_entry *entries;
  size_t capacity;
  /* Immutable admission policy, borrowed from the owning process's group.
   * Set before its first grant; NULL for ordinary ungrouped processes. */
  struct execution_group *execution_group;
};

struct capability_growth_request {
  struct bsp_request request;
  struct capability_table *table;
  enum capability_result result;
};

/* Current user task, IF=0, no held locks. Lends its table until completion.
 * Existing entries/references survive growth failure; no entry pointer may be
 * retained across the call because successful growth replaces their storage. */
enum capability_result capability_request_growth(void);
/* BSP executor, IF=0, with exclusive table ownership. */
void capability_growth_execute(struct capability_growth_request *request);

/* BSP, IF=0, exclusively owning an unsubmitted or caller-lent table.
 * Adds a reference; the caller retains
 * its original one. Resource rights and transport authority are an explicit
 * kernel grant, not derived from another handle, and must fit the object.
 * Unsupported bits are rejected. Failure clears *handle and leaves references
 * unchanged. */
enum capability_result capability_install(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, uint64_t transport,
    handle_t *handle);

/* IF=0, exclusive table ownership on any CPU. Adds a reference without
 * allocating; CAP_FULL leaves the message/owner free to request BSP growth.
 * Other results and ownership match capability_install(). */
enum capability_result capability_insert(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, uint64_t transport,
    handle_t *handle);

/* IF=0, exclusive table ownership. Counts empty slots whose generation has
 * not retired. A NULL table has no free slots. */
size_t capability_free_slots(const struct capability_table *table);

/* IF=0, exclusive table ownership. Installs at most CAPABILITY_BATCH_MAX
 * objects without allocating. The caller owns a reference to each object;
 * successful insertion adds one reference per entry, including duplicates.
 * A failure changes neither the table nor its references. For a valid count,
 * all output handles are cleared before validation and remain invalid on
 * failure. CAP_FULL permits the caller to request BSP growth and retry. */
enum capability_result capability_insert_batch(struct capability_table *table,
    struct kernel_object *const *objects, const uint64_t *rights,
    const uint64_t *transport, size_t count, handle_t *handles);

/* BSP, IF=0, exclusive ownership (including caller loans).
 * Preserves entries, generations and references; failure leaves them intact. */
enum capability_result capability_grow(struct capability_table *table);

/* BSP, IF=0, exclusive ownership of both tables: unsubmitted processes or
 * a blocked launcher caller lending its source table. Copies with equal or
 * reduced resource and transport authority; source remains valid. Failure clears
 * the result handle and changes neither table's entries nor object references. */
enum capability_result capability_grant(struct capability_table *destination,
    struct capability_table *source, handle_t handle, uint64_t rights,
    uint64_t transport, handle_t *result);

/* IF=0, exclusive table ownership (including a BSP loan). Resolve returns a borrowed object, valid only until
 * that entry closes or the table is destroyed; failure clears *object.
 * Both required masks must be present. Optional outputs receive the granted
 * masks on success, zero on failure. A handle has meaning only in its table. */
enum capability_result capability_resolve(struct capability_table *table,
    handle_t handle, uint64_t required_rights, uint64_t required_transport,
    struct kernel_object **object, uint64_t *rights, uint64_t *transport);

/* IF=0, exclusive table ownership; no allocation or backing destruction. Makes
 * the handle stale before release; receipt ownership ends synchronously. */
enum capability_result capability_close(struct capability_table *table,
                                         handle_t handle);

/* BSP, IF=0, with exclusive ownership. Releases all entries and table storage.
 * Backing destruction is deferred to object_reap(); receipt ownership ends
 * synchronously, as with individual handle closure. */
void capability_table_destroy(struct capability_table *table);

#endif
