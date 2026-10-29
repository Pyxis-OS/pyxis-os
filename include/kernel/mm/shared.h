#ifndef KERNEL_MM_SHARED_H
#define KERNEL_MM_SHARED_H

#include <abi/syscall.h>
#include <kernel/mm/types.h>
#include <kernel/object/object.h>

struct shared_origin;
struct shared_region;
struct space;

/* Internal accounting observation, never creation or mapping authority. Physical
 * allocated bytes include quarantine; metadata counts exclude heap overhead. */
struct shared_origin_snapshot {
  struct space *space;
  uint64_t process_key;
  size_t allocated_bytes, quarantined_bytes;
  size_t origin_metadata_bytes, region_metadata_bytes;
};

/* IF=0, kernel root. Returns one owned reference or NULL. The permanent space
 * and non-reused process key identify the origin without retaining a process. */
struct shared_origin *shared_origin_create(struct space *space, uint64_t process_key);
/* Borrowed object pointers; typed owners use ordinary object_retain/release. */
struct kernel_object *shared_origin_object(struct shared_origin *origin);
struct kernel_object *shared_region_object(struct shared_region *region);
struct shared_region *shared_region_from_object(struct kernel_object *object);

/* IF=0, retained caller-CPU kernel-root actor, no short lock/scratch mapping.
 * Nonzero SIZE is already page rounded. Success returns one owned region
 * reference retaining ORIGIN, with fixed, fully zeroed WB RAM. Failure returns
 * no region and frees every unpublished frame before returning. May yield. */
enum call_status shared_region_create(struct shared_origin *origin, size_t size,
    struct shared_region **result);

/* Immutable internal backing access. The caller retains REGION throughout;
 * returned frame addresses never confer authority to a public caller. */
size_t shared_region_size(const struct shared_region *region);
size_t shared_region_page_count(const struct shared_region *region);
phys_addr_t shared_region_frame(const struct shared_region *region, size_t index);
struct shared_origin *shared_region_origin(const struct shared_region *region);
/* IF=0; caller retains ORIGIN. Copies a short guarded observation, no lease. */
void shared_origin_snapshot(struct shared_origin *origin,
    struct shared_origin_snapshot *snapshot);

/* Boot BSP, IF=0, before fixed RAM actors and user admission. */
void shared_memory_init(void);
/* Current creation CPU's existing fixed RAM actor: pending IF=0, reap IF=1.
 * A final region release is safe only after every alias/access has ended.
 * Cleanup attribution ends after actual bounded physical/metadata release. */
bool shared_memory_pending(void);
void shared_memory_reap(void);

#endif
