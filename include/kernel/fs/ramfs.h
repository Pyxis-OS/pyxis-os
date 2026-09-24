#ifndef KERNEL_FS_RAMFS_H
#define KERNEL_FS_RAMFS_H

#include <stddef.h>
#include <stdint.h>

struct directory_entry;

/* BSP, IF=0. Allocate an unpublished entry and its owned empty RAM child.
 * The name buffer is not initialized: the caller fills it before publication.
 * Returns NULL on allocation failure; kind/length are already validated.
 * Disposal is BSP-only; the entry must be unpublished or removed, with no
 * remaining list links or borrowed readers. It releases the child reference. */
struct directory_entry *ramfs_allocate_entry(uint64_t kind, size_t name_length);
/* Prepare name storage only, with a NULL child. Rename transfers its existing
 * child reference into this entry at publication. Same BSP/length contract. */
struct directory_entry *ramfs_allocate_name(size_t name_length);
void ramfs_discard_entry(struct directory_entry *entry);

#endif
