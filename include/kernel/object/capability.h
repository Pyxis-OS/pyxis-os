#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <stddef.h>
#include <abi/handle.h>
#include <kernel/service/request.h>
#include <kernel/spinlock.h>

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
 * task with IF=0. During growth, launch, network-open, host-create or screen
 * capture requests the task lends
 * the table to the BSP until completion. The guard orders lookup/detachment and
 * structural publication, not concurrent growth, installation or teardown;
 * those still require this exclusive ownership. */
struct capability_table {
  /* IF=0; nests no object, scheduler or allocator lock. */
  struct spinlock lock;
  struct capability_entry *entries;
  size_t capacity;
  /* Immutable admission policy, borrowed from the owning process's group.
   * Set before its first grant; NULL for ordinary ungrouped processes. */
  struct execution_group *execution_group;
};

struct capability_reference {
  struct kernel_object *object; /* Owned storage, not grant authority. */
  uint64_t rights;
  uint64_t transport;
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
size_t capability_free_slots(struct capability_table *table);

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

/* IF=0; caller keeps the table alive. Atomically validates the exact generation
 * and required masks, captures rights/transport and retains object storage.
 * Failure clears reference; CAP_LIMIT means storage-reference saturation.
 * CLOSE may end logical ownership while admitted storage remains safe to use.
 * The caller releases after its operation and blocking continuations finish. */
enum capability_result capability_acquire(struct capability_table *table,
    handle_t handle, uint64_t required_rights, uint64_t required_transport,
    struct capability_reference *reference);
/* IF=0; consumes and clears an owned reference. An empty reference is allowed. */
void capability_release(struct capability_reference *reference);

/* IF=0, exclusive table ownership; no allocation or backing destruction. Makes
 * the exact generation stale before applying close effects outside the guard;
 * receipt ownership ends synchronously. No new storage reference is required. */
enum capability_result capability_close(struct capability_table *table,
                                         handle_t handle);

/* BSP, IF=0, with exclusive ownership. Releases all entries and table storage.
 * Backing destruction is deferred to object_reap(); receipt ownership ends
 * synchronously, as with individual handle closure. */
void capability_table_destroy(struct capability_table *table);

#endif
