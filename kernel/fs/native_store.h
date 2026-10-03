/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_FS_NATIVE_STORE_H
#define KERNEL_FS_NATIVE_STORE_H

#include <abi/directory.h>
#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/gpt.h>
#include <pyxis_fs/native.h>

struct native_store_pool;
struct native_store_volume;
struct native_store_inode;

/* Every entry runs on the sole nativefs BSP worker with IF=1. No pointer escapes
 * to another task. Context is borrowed for one call, never retained across it.
 * The caller supplies its wall-clock reading; unknown timestamps are explicit.
 * Operation diagnostics describe the first backing failure in this call. */
struct native_store_context {
  uint64_t deadline;
  enum block_result backing_error;
  enum pnf_status format_error;
  bool time_valid;
  int64_t time_ns;
};

enum call_status native_store_open(struct native_store_context *context,
    const struct gpt_partition *partition, const struct block_info *device,
    bool writable, struct native_store_pool **out);
enum call_status native_store_upgrade(struct native_store_context *context,
    struct native_store_pool *pool);
const uint8_t *native_store_pool_id(const struct native_store_pool *pool);
bool native_store_writable(const struct native_store_pool *pool);
enum call_status native_store_root(struct native_store_context *context,
    struct native_store_pool *pool, const char *name, size_t length,
    struct native_store_inode **out);
/* A successful root/lookup/create returns one owned inode reference. References
 * count handles and retained operations; dirty/cleanup storage lives separately.
 * Release neither flushes nor relinquishes persistent pool state. */
void native_store_retain(struct native_store_inode *inode);
/* Consumes the reference; inode storage may be freed. */
void native_store_release(struct native_store_inode *inode);
uint64_t native_store_kind(const struct native_store_inode *inode);
bool native_store_same_volume(const struct native_store_inode *a,
    const struct native_store_inode *b);
enum call_status native_store_lookup(struct native_store_context *context,
    struct native_store_inode *directory, const char *name, size_t length,
    struct native_store_inode **out);
/* Generation is nonzero and shared by inode handles. Mismatch returns OK with
 * an empty entry/current generation; inode zero otherwise denotes END. Invalid
 * byte positions fail without advancing. */
enum call_status native_store_enumerate(struct native_store_context *context,
    struct native_store_inode *directory, uint64_t generation, uint64_t position,
    struct pnf_dirent *entry, uint64_t *next_position, uint64_t *current_generation);
enum call_status native_store_read(struct native_store_context *context,
    struct native_store_inode *inode, uint64_t offset, void *bytes,
    size_t capacity, size_t *read);
uint64_t native_store_size(const struct native_store_inode *inode);
/* Create/rename flush prior dirty files before privately preparing an indivisible
 * metadata commit. Remove uses durable target metadata and can free space while
 * delayed-allocation file writeback is blocked. */
enum call_status native_store_create(struct native_store_context *context,
    struct native_store_inode *directory, const char *name, size_t length,
    uint64_t kind, struct native_store_inode **out);
enum call_status native_store_remove(struct native_store_context *context,
    struct native_store_inode *directory, const char *name, size_t length, uint64_t kind);
enum call_status native_store_rename(struct native_store_context *context,
    struct native_store_inode *source, const char *source_name, size_t source_length,
    struct native_store_inode *destination, const char *destination_name,
    size_t destination_length, bool replace);
enum call_status native_store_write(struct native_store_context *context,
    struct native_store_inode *inode, uint64_t offset, const void *bytes,
    size_t length, size_t *written);
enum call_status native_store_resize(struct native_store_context *context,
    struct native_store_inode *inode, uint64_t size);
/* Whole-current-pool durability returns at COMMITTED. Background maintenance
 * checkpoints stable images and publishes EMPTY before starting another commit.
 * Empty sync still reports retained writeback failure. */
enum call_status native_store_sync(struct native_store_context *context,
    struct native_store_pool *pool);
struct native_store_pool *native_store_inode_pool(struct native_store_inode *inode);
enum call_status native_store_maintain(struct native_store_context *context,
    struct native_store_pool *pool, bool flush_dirty, bool pressure);
bool native_store_pending(const struct native_store_pool *pool);
void native_store_info(struct native_store_inode *inode,
    struct directory_filesystem_info *info);

#endif
