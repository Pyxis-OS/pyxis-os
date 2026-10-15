#ifndef KERNEL_FS_EXTENTS_H
#define KERNEL_FS_EXTENTS_H

#include <abi/syscall.h>
#include <kernel/block.h>

enum storage_extent_kind {
  STORAGE_EXTENT_PENDING_NPFS,
  STORAGE_EXTENT_NPFS,
  STORAGE_EXTENT_PENDING_FAT,
  STORAGE_EXTENT_FAT,
  STORAGE_EXTENT_RAW,
};

struct storage_extent {
  struct storage_extent *next;
  block_device_id device;
  uint64_t first_block, block_count;
  enum storage_extent_kind kind;
};

/* Filesystem worker only, IF=1. The caller owns the embedded record and keeps
 * it alive from successful reservation through release. No allocation or waits.
 * Ranges use checked half-open logical blocks in the inventoried device.
 * Pending mounts exclude raw claims and other admissions just like live mounts.
 * RAW also preserves the one-claim-per-device restriction for disjoint ranges. */
enum call_status storage_extent_reserve(struct storage_extent *extent,
    block_device_id device, uint64_t first_block, uint64_t block_count,
    enum storage_extent_kind kind);
/* Only the matching pending NPFS/FAT kind may be published. */
void storage_extent_publish(struct storage_extent *extent, enum storage_extent_kind kind);
void storage_extent_release(struct storage_extent *extent);
/* Transfer a live RAW reservation into previously unused owned storage and
 * narrow its range without allocation, device probing or an exclusion gap.
 * The caller already validated the contained range before external effects. */
void storage_extent_transfer_raw(struct storage_extent *source,
    struct storage_extent *destination, uint64_t first_block, uint64_t block_count);
bool storage_extent_device_mounted(block_device_id device);
bool storage_extent_range_mounted(block_device_id device, uint64_t first_block,
    uint64_t block_count);
bool storage_extent_device_claimed(block_device_id device);
/* Borrowed until the next worker registry mutation; never retain across waits. */
const struct storage_extent *storage_extent_device_claim(block_device_id device);

#endif
