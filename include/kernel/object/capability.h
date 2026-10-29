#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <stddef.h>
#include <abi/handle.h>
#include <kernel/service/request.h>
#include <kernel/spinlock.h>

struct kernel_object;
struct capability_entry;
struct execution_group;
struct forwarder_operation;

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
  CAP_NOT_FOUND,
};

/* Zero initialization creates an empty table. Admitted activity keeps it alive;
 * teardown follows activity and reservation drain. Growth retains table ownership
 * across allocation. No entry pointer survives the short structural guard. */
struct capability_table {
  /* IF=0; may nest only the bounded forwarder target-snapshot guard for
   * atomic storage admission. No callbacks, scheduler or allocator lock. */
  struct spinlock lock;
  struct capability_entry *entries;
  size_t capacity;
  size_t reserved;
  /* Immutable admission policy, borrowed from the owning process's group.
   * Set before its first grant; NULL for ordinary ungrouped processes. */
  struct execution_group *execution_group;
};

struct capability_reference {
  struct kernel_object *object; /* Owned storage, not grant authority. */
  uint64_t rights;
  uint64_t transport;
  uint64_t restrictions; /* Held grant's; resolution adds the target lineage's. */
  struct kernel_object *forwarder; /* Original wrapper storage after resolution. */
  struct kernel_object *admitted; /* Target pin captured with the source generation. */
};

/* Restrictions are monotonic: a grant re-created from an existing grant or
 * reference keeps all of the source's restrictions (capability_grant_derive). */
struct capability_grant {
  struct kernel_object *object; /* Owned logical grant, including storage. */
  uint64_t rights;
  uint64_t transport;
  uint64_t restrictions;
};

struct capability_reserved_slot {
  size_t index;
  uint32_t generation;
};

struct capability_reservation {
  struct capability_table *table; /* Borrowed for admitted activity's lifetime. */
  size_t count;
};

struct capability_growth_request {
  struct bsp_request request;
  struct capability_table *table;
  enum capability_result result;
};

/* Current user task, IF=0, no held locks. Admitted activity keeps the table live.
 * Existing entries/references survive growth failure; no entry pointer may be
 * retained across the call because successful growth replaces their storage. */
enum capability_result capability_request_growth(void);
/* BSP executor, IF=0. */
void capability_growth_execute(struct capability_growth_request *request);

/* IF=0; caller supplies count slots in stable owned storage. Claims every slot
 * or none without allocating. Empty claims are invisible to handle lookup.
 * A live reservation has one owner: move its metadata and slots together and
 * clear the source. Never copy ownership or release/consume it twice. */
enum capability_result capability_reserve(struct capability_table *table,
    size_t count, struct capability_reservation *reservation,
    struct capability_reserved_slot *slots);
/* Consumes a live reservation; an empty token is a harmless no-op. */
void capability_reservation_release(struct capability_reservation *reservation,
    const struct capability_reserved_slot *slots);
/* Current task, no held locks or prepared BSP request. Retries reservation with
 * BSP growth. The caller checks stop and unwinds before its next side effect. */
enum capability_result capability_request_reservation(size_t count,
    struct capability_reservation *reservation,
    struct capability_reserved_slot *slots);

/* Source-handle admission obtains prospective logical ownership outside the
 * table guard, then rechecks the exact source generation and authority. A source
 * closed before that recheck fails BAD_HANDLE; later close cannot revoke the
 * admitted grant. Receipts and receivers cannot transfer. Failure clears grant. */
enum capability_result capability_grant_acquire(struct capability_table *source,
    handle_t handle, uint64_t rights, uint64_t transport,
    struct capability_grant *grant);
enum capability_result capability_grant_acquire_same(struct capability_table *source,
    handle_t handle, struct capability_grant *grant);
/* Explicit kernel grant; permits native receiver/receipt creation. Starts
 * unrestricted: only for a new object or authority no held grant restricts. */
enum capability_result capability_grant_retain(struct kernel_object *object,
    uint64_t rights, uint64_t transport, struct capability_grant *grant);
/* Kernel grant re-created from an existing grant or reference to the same
 * object. The caller passes the source's restrictions plus any it adds. */
enum capability_result capability_grant_derive(struct kernel_object *object,
    uint64_t rights, uint64_t transport, uint64_t restrictions,
    struct capability_grant *grant);
void capability_grant_release(struct capability_grant *grant);

/* Current admitted table, IF=0. Atomically detach exact source generations into
 * owned grants without CLOSE effects. References are retained metadata captures,
 * not resolved targets. All sources move or none; failure leaves entries intact.
 * Call only after destination admission. No allocation, callbacks or waits. */
enum capability_result capability_grant_take(struct capability_table *table,
    const handle_t *handles, const struct capability_reference *references,
    size_t count, struct capability_grant *grants);

/* Immutable object masks and destination group policy; no ownership change. */
enum capability_result capability_validate_grants(const struct capability_table *table,
    const struct capability_grant *grants, size_t count);
/* Grants must already pass destination validation. Nonfallibly publishes every
 * supplied grant, consumes its existing ownership and the reservation, and
 * releases unused slots. No callbacks, allocation or new retains. */
void capability_install_reserved(struct capability_reservation *reservation,
    const struct capability_reserved_slot *slots, struct capability_grant *grants,
    size_t count, handle_t *handles);

/* IF=0; caller keeps the table alive.
 * Adds a reference; the caller retains
 * its original one. Resource rights and transport authority are an explicit
 * kernel grant, not derived from another handle, and must fit the object.
 * Unsupported bits are rejected. Failure clears *handle and leaves references
 * unchanged. */
enum capability_result capability_install(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, uint64_t transport,
    handle_t *handle);

/* IF=0, admitted table activity on any CPU. Adds a reference without
 * allocating; CAP_FULL leaves the message/owner free to request BSP growth.
 * Other results and ownership match capability_install(). */
enum capability_result capability_insert(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, uint64_t transport,
    handle_t *handle);

/* IF=0, admitted table activity. Installs at most CAPABILITY_BATCH_MAX
 * objects without allocating. The caller owns a reference to each object;
 * successful insertion adds one reference per entry, including duplicates.
 * A failure changes neither the table nor its references. For a valid count,
 * all output handles are cleared before validation and remain invalid on
 * failure. CAP_FULL permits the caller to request BSP growth and retry. */
enum capability_result capability_insert_batch(struct capability_table *table,
    struct kernel_object *const *objects, const uint64_t *rights,
    const uint64_t *transport, size_t count, handle_t *handles);

/* IF=0; caller keeps the table alive.
 * Preserves entries, generations and references; failure leaves them intact. */
enum capability_result capability_grow(struct capability_table *table);

/* IF=0; caller keeps the table alive. Atomically validates the exact generation
 * and required masks, captures rights/transport and retains object storage.
 * Failure clears reference; CAP_LIMIT means storage-reference saturation.
 * CLOSE may end logical ownership while admitted storage remains safe to use.
 * The caller releases after its operation and blocking continuations finish. */
enum capability_result capability_acquire(struct capability_table *table,
    handle_t handle, uint64_t required_rights, uint64_t required_transport,
    struct capability_reference *reference);
/* Metadata-only observation of the held entry, even after CUT. Retains wrapper
 * storage but admits no operation and extracts no target authority. */
enum capability_result capability_acquire_metadata(struct capability_table *table,
    handle_t handle, struct capability_reference *reference);
/* IF=0; consumes and clears an owned reference. An empty reference is allowed. */
void capability_release(struct capability_reference *reference);
/* Operation-only resolution; COPY/launch/IPC grant capture never calls this.
 * Keeps wrapper storage and adds lineage to the current task's operation. */
enum capability_result capability_reference_resolve(struct capability_reference *reference);
/* IF=0; captures only this reference's lineage into an initialized, empty
 * PENDING operation owned by the caller. Never borrows current-task lineage.
 * Release the operation even after resolution failure. */
enum capability_result capability_reference_resolve_for(struct capability_reference *reference,
    struct forwarder_operation *operation);

/* IF=0, admitted table activity; no allocation or backing destruction. Makes
 * the exact generation stale before applying close effects outside the guard;
 * receipt ownership ends synchronously. No new storage reference is required. */
enum capability_result capability_close(struct capability_table *table,
                                         handle_t handle);

/* IF=0, after admitted activity/reservations drain. Releases table storage.
 * Backing destruction is deferred to object_reap(); receipt ownership ends
 * synchronously, as with individual handle closure. */
void capability_table_destroy(struct capability_table *table);

/* Revoke only newly returned handles owned by a failed output operation. Exact
 * handle generations prevent touching a reused slot; an already closed handle
 * is harmless. This does not undo external effects that created the objects. */
void capability_close_returned(struct capability_table *table,
    const handle_t *handles, size_t count);

#endif
