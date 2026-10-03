#ifndef KERNEL_OBJECT_DISK_H
#define KERNEL_OBJECT_DISK_H

#include <abi/disk.h>
#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/object/object.h>

struct npfs_job;
struct npfs_store_context;
struct execution_group;
struct disk_object {
  struct kernel_object object;
  block_device_id device;
  bool claimed;
  struct disk_object *claim_next, *retired_next;
  struct execution_group *cleanup_group;
};

struct kernel_object *disks_create(void);
struct syscall_result disks_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result disk_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

/* Filesystem worker only, IF=1. Raw claims and pool registration share its queue. */
bool disk_device_claimed(block_device_id device);
enum call_status disk_perform(struct npfs_store_context *context, struct npfs_job *job);
void disk_cleanup_retired(struct disk_object *disks);
/* BSP/IF=0: allocation-free retirement transfer to the owning worker. */
struct disk_object *disk_take_retired(void);
void disk_retire(struct disk_object *disk);

#endif
