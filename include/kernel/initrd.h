#ifndef KERNEL_INITRD_H
#define KERNEL_INITRD_H

#include <stddef.h>

struct boot_module;

enum initrd_result {
  INITRD_OK,
  INITRD_INVALID,
  INITRD_UNSUPPORTED,
  INITRD_NO_MEMORY,
  INITRD_NOT_FOUND,
  INITRD_END,
};

struct initrd_file {
  const void *data;
  size_t size;
};

struct initrd_entry {
  const char *name;
  size_t name_length;
  struct initrd_file file;
  bool directory;
};

/* Start offset at zero; reuse only offsets returned by this iterator. All
 * views borrow archive storage. Returns INITRD_END at/after the trailer.
 * No allocation or path interpretation; clears *entry unless returning OK. */
enum initrd_result initrd_next(size_t *offset, struct initrd_entry *entry);

/* Once on the BSP, IF=0, after VM initialization and before AP startup.
 * Map and validate one uncompressed newc archive with a TRAILER!!! terminator.
 * Supports regular files without hard links and directory records; rejects
 * other entry types. Ownership, permissions and timestamps are not applied.
 * Success retains a read-only mapping of borrowed boot-reserved frames for
 * the kernel lifetime. Failure removes partial mappings and reservations. */
enum initrd_result initrd_init(const struct boot_module *module);

/* Exact archive-name lookup, without path normalization or directory traversal.
 * Returns the first matching regular file. The view is borrowed, immutable and
 * valid for the kernel lifetime; do not free it. No allocation or copying.
 * Requires successful initialization. Failure clears *file when non-NULL. */
enum initrd_result initrd_lookup(const char *name, struct initrd_file *file);

#endif
