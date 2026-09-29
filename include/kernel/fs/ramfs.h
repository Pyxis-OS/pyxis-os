#ifndef KERNEL_FS_RAMFS_H
#define KERNEL_FS_RAMFS_H

#include <kernel/service/request.h>
#include <stddef.h>
#include <stdint.h>

struct directory_entry;

enum ramfs_request_operation {
  RAMFS_ALLOCATE_ENTRY,
  RAMFS_ALLOCATE_NAME,
  RAMFS_DISCARD,
};

struct ramfs_request {
  struct bsp_request request;
  enum ramfs_request_operation operation;
  uint64_t kind;
  size_t name_length;
  struct directory_entry *entry; /* Owned result, or transferred for disposal. */
};

/* Current user task, IF=0, no held locks. Kind/length are already validated.
 * Returns an unpublished entry with an owned empty RAM child, or NULL. The
 * caller fills the name before publishing or transfers the entry for disposal. */
struct directory_entry *ramfs_request_entry(uint64_t kind, size_t name_length);
/* Name storage only, with a NULL child until rename transfers its reference. */
struct directory_entry *ramfs_request_name(size_t name_length);
/* Transfers an unpublished/removed entry with no list links or borrowed readers.
 * Submission allocates nothing, including after an allocation failure. */
void ramfs_request_discard(struct directory_entry *entry);

/* BSP executor, IF=0. Discard releases the child reference and frees the entry. */
void ramfs_request_execute(struct ramfs_request *request);

#endif
