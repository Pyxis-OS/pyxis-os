#ifndef KERNEL_FS_FAT32_H
#define KERNEL_FS_FAT32_H

#include <kernel/fs/npfs.h>
#include <kernel/object/mount.h>

#define FAT32_ROOT_RIGHTS (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | \
    DIRECTORY_RIGHT_READ_FILES)
struct npfs_store_context;
struct fat32_node;

/* Sole BSP filesystem worker, IF=1. */
enum call_status fat32_perform(struct npfs_store_context *context, struct npfs_job *job);
/* Any caller, IF=0, live node: reference-admission handoff only. Payload remains
 * worker-owned; queued control operations count toward retirement drainage. */
/* One-way publication receipt; worker consumes it before final cleanup. */
void fat32_mark_returned(struct fat32_node *node);
void fat32_request_published(struct fat32_node *node);
void fat32_request_complete(struct fat32_node *node);
/* BSP/IF=0: allocation-free final destruction handoff. */
void fat32_retire_node(struct fat32_node *node);
struct fat32_node *fat32_take_retired(void);
/* Sole worker, IF=1. */
void fat32_cleanup_nodes(struct fat32_node *nodes);
/* Native control/issuer dispatch, caller holds a live object through waits. */
struct syscall_result fat32_mount_call(struct mount_object *mount, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result fat32_open_device(block_device_id device,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
