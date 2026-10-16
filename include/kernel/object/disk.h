#ifndef KERNEL_OBJECT_DISK_H
#define KERNEL_OBJECT_DISK_H

#include <abi/disk.h>
#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/fs/extents.h>
#include <kernel/object/object.h>

struct npfs_job;
struct npfs_store_context;
struct execution_group;
struct gpt_image;
enum disk_kind {
  DISK_RAW, DISK_POOL, DISK_RAW_PARTITION,
  DISK_ERASE_REVIEW, DISK_APPEND_REVIEW, DISK_ERASE_WRITER,
  DISK_ESP_REVIEW, DISK_ESP_WRITER,
};
struct disk_object {
  struct kernel_object object;
  block_device_id device;
  bool claimed;
  enum disk_kind kind;
  /* Immutable selection is authority; the removable extent is only a claim. */
  struct disk_partition_selection binding;
  struct gpt_image *review;
  bool review_spent;
  /* Claim scope: GPT entry, or zero for the whole disk, and its block range. */
  uint32_t partition;
  struct storage_extent extent;
  struct disk_object *retired_next;
  struct execution_group *cleanup_group;
};

struct kernel_object *disks_create(void);
bool disk_rights_valid(const struct disk_object *disk, uint64_t rights);
enum call_status disk_validate_binding(struct disk_object *disk, uint32_t partition);
struct syscall_result disks_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result disk_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

/* Filesystem worker only, IF=1. Raw claims and pool registration share its queue. */
bool disk_device_claimed(block_device_id device);
/* Whole-disk claim, or physical overlap with the selected GPT entry. */
bool disk_partition_claimed(block_device_id device, uint32_t partition);
enum call_status disk_perform(struct npfs_store_context *context, struct npfs_job *job);
void disk_cleanup_retired(struct disk_object *disks);
/* BSP/IF=0: allocation-free retirement transfer to the owning worker. */
struct disk_object *disk_take_retired(void);
bool disk_retired_pending(void);
void disk_retire(struct disk_object *disk);

#endif
