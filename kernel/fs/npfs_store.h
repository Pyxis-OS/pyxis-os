/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_FS_NPFS_STORE_H
#define KERNEL_FS_NPFS_STORE_H

#include <abi/directory.h>
#include <abi/file_info.h>
#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/gpt.h>
#include <pyxis_fs/npfs.h>

struct npfs_store_pool;
struct npfs_store_volume;
struct npfs_store_inode;
struct native_completion;

/* Every entry runs on the sole npfs BSP worker with IF=1. No pointer escapes
 * to another task. Context is borrowed for one call, never retained across it.
 * The caller supplies its wall-clock reading; unknown timestamps are explicit.
 * Operation diagnostics describe the first backing failure in this call. */
struct npfs_store_context {
  uint64_t deadline;
  enum block_result backing_error;
  enum npfs_status format_error;
  bool time_valid;
  bool bulk_read; /* Per-page fairness and exact interruption for owned bulk reads. */
  int64_t time_ns;
  struct native_completion *completion; /* Optional owned cancellation control. */
};

enum call_status npfs_store_open(struct npfs_store_context *context,
    block_device_id device_id, const struct gpt_partition *partition, const struct block_info *device,
    bool writable, struct npfs_store_pool **out);
enum call_status npfs_store_open_ram(struct npfs_store_context *context,
    uint64_t capacity, struct npfs_store_pool **out);
enum call_status npfs_store_upgrade(struct npfs_store_context *context,
    struct npfs_store_pool *pool);
const uint8_t *npfs_store_pool_id(const struct npfs_store_pool *pool);
bool npfs_store_writable(const struct npfs_store_pool *pool);
enum call_status npfs_store_root(struct npfs_store_context *context,
    struct npfs_store_pool *pool, const char *name, size_t length,
    struct npfs_store_inode **out);
/* Appends a live volume NAME with identity ID and an empty root directory, in
 * one committed transaction. A taken name is ALREADY_EXISTS; a full catalog is
 * LIMIT. The volume is not mounted; npfs_store_root opens it. */
enum call_status npfs_store_create_volume(struct npfs_store_context *context,
    struct npfs_store_pool *pool, const char *name, size_t length,
    const uint8_t id[NPFS_ID_SIZE]);
/* A successful root/lookup/create returns one owned inode reference. References
 * count handles and retained operations; dirty/cleanup storage lives separately.
 * Release neither flushes nor relinquishes persistent pool state. */
void npfs_store_retain(struct npfs_store_inode *inode);
/* Consumes the reference; inode storage may be freed. */
void npfs_store_release(struct npfs_store_inode *inode);
uint64_t npfs_store_kind(const struct npfs_store_inode *inode);
bool npfs_store_same_volume(const struct npfs_store_inode *a,
    const struct npfs_store_inode *b);
bool npfs_store_is_root(const struct npfs_store_inode *inode);
enum call_status npfs_store_lookup(struct npfs_store_context *context,
    struct npfs_store_inode *directory, const char *name, size_t length,
    struct npfs_store_inode **out);
/* Generation is nonzero and shared by inode handles. Mismatch returns OK with
 * an empty entry/current generation; inode zero otherwise denotes END. Invalid
 * byte positions fail without advancing. */
enum call_status npfs_store_enumerate(struct npfs_store_context *context,
    struct npfs_store_inode *directory, uint64_t generation, uint64_t position,
    struct npfs_dirent *entry, uint64_t *next_position, uint64_t *current_generation);
enum call_status npfs_store_read(struct npfs_store_context *context,
    struct npfs_store_inode *inode, uint64_t offset, void *bytes,
    size_t capacity, size_t *read);
uint64_t npfs_store_size(const struct npfs_store_inode *inode);
/* Current inode data, including accepted cached writes. Identity follows live
 * inode references across wrappers; retired/free slots acquire a new identity. */
void npfs_store_file_info(struct npfs_store_inode *inode, struct file_info_reply *info);
/* Namespace edits join the pool's running transaction and return before it
 * commits; sync, maintenance, power-off or journal capacity commit it. They
 * never split across commits. Replacement rename first writes back the moved
 * file and stages its mapping.
 * LINK adds a name to one regular inode; remove/replacement decrement its count.
 * Only last-name removal detaches the last staged record, retaining unpublished
 * contents for open handles and later writeback. Count edits preserve the last
 * staged size/mapping, without publishing cached growth. Same-inode rename is a
 * successful no-op even between distinct names and under NO_REPLACE.
 * Directory rename preserves the inode/subtree and
 * journals its cross-parent backlink with both names; replacement requires an
 * empty same-kind victim. Ancestry validation is bounded and exposes no parent
 * authority. Moves do not flush dirty descendants. */
enum call_status npfs_store_create(struct npfs_store_context *context,
    struct npfs_store_inode *directory, const char *name, size_t length,
    uint64_t kind, struct npfs_store_inode **out);
enum call_status npfs_store_remove(struct npfs_store_context *context,
    struct npfs_store_inode *directory, const char *name, size_t length, uint64_t kind);
enum call_status npfs_store_link(struct npfs_store_context *context,
    struct npfs_store_inode *source, const char *source_name, size_t source_length,
    struct npfs_store_inode *destination, const char *destination_name, size_t destination_length);
enum call_status npfs_store_rename(struct npfs_store_context *context,
    struct npfs_store_inode *source, const char *source_name, size_t source_length,
    struct npfs_store_inode *destination, const char *destination_name,
    size_t destination_length, bool replace, uint64_t kind);
/* A later cache failure returns CALL_OK with the accepted prefix in written. */
enum call_status npfs_store_write(struct npfs_store_context *context,
    struct npfs_store_inode *inode, uint64_t offset, const void *bytes,
    size_t length, size_t *written);
/* Growth is cached. Shrink flushes only this inode before staging its smaller
 * size; cached writes to unrelated files remain pending for sync or maintenance. */
enum call_status npfs_store_resize(struct npfs_store_context *context,
    struct npfs_store_inode *inode, uint64_t size);
/* Whole-current-pool durability returns at COMMITTED. Background maintenance
 * checkpoints stable images and publishes EMPTY before starting another commit.
 * Empty sync still reports retained writeback failure. */
enum call_status npfs_store_sync(struct npfs_store_context *context,
    struct npfs_store_pool *pool);
/* Flushes this file's data, then commits the running transaction with every
 * staged namespace edit. Other files' cached data stays pending. Reports and
 * acknowledges only this file's retained writeback error; pool sync still
 * reports every file's. */
enum call_status npfs_store_sync_file(struct npfs_store_context *context,
    struct npfs_store_inode *inode);
struct npfs_store_pool *npfs_store_inode_pool(struct npfs_store_inode *inode);
enum call_status npfs_store_maintain(struct npfs_store_context *context,
    struct npfs_store_pool *pool, bool flush_dirty, bool pressure);
bool npfs_store_pending(const struct npfs_store_pool *pool);
/* True when the durable journal control is EMPTY and nothing is staged. */
bool npfs_store_journal_empty(const struct npfs_store_pool *pool);
void npfs_store_info(struct npfs_store_inode *inode,
    struct directory_filesystem_info *info);

#endif
