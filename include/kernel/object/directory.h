#ifndef KERNEL_DIRECTORY_H
#define KERNEL_DIRECTORY_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct hostfs_node;
struct nativefs_node;

enum directory_backing {
  DIRECTORY_INITRD,
  DIRECTORY_RAM,
  DIRECTORY_HOST,
  DIRECTORY_NATIVE,
};

struct directory_entry {
  struct directory_entry *next;
  struct kernel_object *object; /* Owned child; NULL only in unlinked rename storage. */
  size_t name_length;
  char name[];
};

/* The lock protects links/count/generation/detached. Entries own child
 * references; children do not retain parents. Removed empty directories stay
 * detached and reject creation through surviving handles. Multi-directory
 * mutations take the mutation lock before any directory locks. Only the
 * unpublished initrd builder bypasses locking. Native directories use only
 * their worker-owned node, never these in-memory links or the lock. */
struct directory_object {
  struct kernel_object object;
  enum directory_backing backing;
  struct hostfs_node *host; /* Owned by the deferred host worker destructor. */
  struct nativefs_node *native; /* Worker owns this wrapper and its core view. */
  atomic_bool locked;
  bool detached;
  struct directory_entry *first, *last;
  size_t entry_count;
  uint64_t generation;
};

/* BSP, IF=0. Return one owned reference to an empty directory, or NULL. Final
 * BSP destruction frees names/entries and retires owned children, without
 * recursing through the C stack. Child files manage their own backing lifetime. */
struct directory_object *directory_create(enum directory_backing backing);

/* BSP worker, IF=0. Initialize embedded storage without allocating. Final
 * destruction retires the node; it does not free the directory separately. */
void directory_init_native(struct directory_object *directory, struct nativefs_node *node);

/* Current user task, IF=0, with a live reference and stable private mappings.
 * Lookup retains the child before releasing the lock or waiting for BSP table
 * growth. CREATE waits for BSP entry allocation/disposal with no locks held.
 * Enumeration copies the selected name while locked. REMOVE detaches an entry
 * under the lock and then lends it to BSP disposal, with no borrowed readers.
 * RENAME stages name storage on BSP, rechecks both parents under their locks,
 * transfers the child reference and disposes obsolete entries after unlocking. */
struct syscall_result directory_call(struct directory_object *directory, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
