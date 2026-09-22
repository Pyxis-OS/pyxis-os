#ifndef KERNEL_FS_INITRD_TREE_H
#define KERNEL_FS_INITRD_TREE_H

#include <kernel/initrd.h>

struct directory_object;

/* BSP, IF=0, after heap/initrd initialization. Build an unpublished tree whose
 * files borrow archive data. Infer parent directories; merge explicit directory
 * records, including empty ones. Ignore leading ./ and a directory's trailing /;
 * reject absolute paths, interior empty/dot/dot-dot components, duplicate files
 * and file/directory conflicts. Success returns one owned root reference.
 * Failure clears *root and releases partial objects for BSP retirement. */
enum initrd_result initrd_tree_create(struct directory_object **root);

#endif
