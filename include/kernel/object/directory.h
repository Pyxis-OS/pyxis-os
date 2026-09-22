#ifndef KERNEL_DIRECTORY_H
#define KERNEL_DIRECTORY_H

#include <abi/syscall.h>
#include <kernel/object/object.h>

struct directory_entry {
  struct directory_entry *next;
  struct kernel_object *object; /* One owned child reference. */
  size_t name_length;
  char name[];
};

/* Constructed on the BSP and immutable once published. Entries own their child
 * objects; children do not reference parents. A child handle can outlive its
 * parent. Only the unpublished initrd tree builder modifies these fields. */
struct directory_object {
  struct kernel_object object;
  struct directory_entry *first, *last;
  size_t entry_count;
  uint64_t generation;
};

/* BSP, IF=0. Return one owned reference to an empty directory, or NULL. Final
 * BSP destruction frees names/entries and retires owned children, without
 * recursing through the C stack. File bytes remain owned by the archive. */
struct directory_object *directory_create(void);

/* Current user task, IF=0, with a live reference and stable private mappings.
 * Lookup may block for BSP table growth, holding no locks. Immutable entries
 * and the caller's directory reference keep the selected child alive across
 * that wait. Revisit synchronization/lifetime before adding mutable backing. */
struct syscall_result directory_call(struct directory_object *directory, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
