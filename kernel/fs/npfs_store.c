/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/mm/kernel_owner.h>
#include "npfs_store.h"
#include "npfs_ram.h"

#include <abi/mount.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/fs/metadata.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/completion.h>

/* Stop only between cache chunks or before namespace mutation. Journal
 * publication/checkpointing and an admitted physical ticket always drain. */
static enum call_status completion_interruption(struct npfs_store_context *context)
{
  if (context->completion) {
    enum call_status reason = native_completion_cancel_reason(context->completion);
    if (reason != CALL_OK) {
      return reason;
    }
  }
  if (context->bulk_read) {
    if (task_operation_denied()) {
      return CALL_DENIED;
    }
    if (task_deadline_expired(context->deadline)) {
      return CALL_TIMED_OUT;
    }
  }
  if (!context->completion) {
    return CALL_OK;
  }
  return task_deadline_expired(context->deadline) ? CALL_TIMED_OUT : CALL_OK;
}

#define STORE_IMAGES_MAX 128u
#define STORE_CACHE_CHUNKS 4u
#define STORE_CHUNK_ENTRIES 256u
#define STORE_CLEANUP_IMAGES 10u
#define STORE_CLEANUP_MAPPINGS 64u
/* One mapping (three indirect blocks, four bitmap pages) plus the inode block. */
#define STORE_WRITEBACK_IMAGES 10u
/* One mapping allocates at most three indirect blocks and its target. */
#define STORE_MAPPING_BLOCKS 4u
/* The largest namespace operation: create extends the inode file and its
 * directory (eight images each) and stages the volume and parent records. */
#define STORE_OPERATION_IMAGES MOUNT_NPFS_MIN_JOURNAL_IMAGES
#define STORE_OPERATION_BLOCKS (2u * STORE_MAPPING_BLOCKS)
#define STORE_BITMAP_READ_BLOCKS 128u
#define STORE_WRITE_RUN_BLOCKS 32u
#define STORE_READ_RUN_BLOCKS 16u
#define STORE_METADATA_CHUNKS 4u
#define STORE_METADATA_CHUNK_PAGES 32u
#define STORE_METADATA_PAGES (STORE_METADATA_CHUNKS * STORE_METADATA_CHUNK_PAGES)
#define STORE_ANCESTRY_STEPS 1024u

_Static_assert(STORE_IMAGES_MAX <= NPFS_DESCRIPTORS_PER_BLOCK,
  "native journal writes one descriptor block");

struct store_free_slot {
  struct store_free_slot *next;
  uint64_t number;
  bool heap_owned;
};

struct npfs_store_volume {
  struct npfs_store_volume *next;
  struct npfs_store_pool *pool;
  struct npfs_volume record;
  uint64_t domain;
  unsigned slot;
  struct store_free_slot *free_slots;
  struct npfs_store_inode *inodes;
};

struct npfs_store_inode {
  struct npfs_store_inode *next;
  struct npfs_store_volume *volume;
  /* durable is the record as the running transaction will commit it, or as
   * already committed; recovery yields it once that transaction is durable. */
  struct npfs_inode record, durable;
  uint64_t number, generation, durable_size, identity;
  size_t references;
  /* This file's unreported writeback failure, for its own FILE sync. */
  enum call_status writeback_error;
  bool size_dirty;
  struct store_free_slot recycled_slot;
};

struct store_cache_entry {
  struct npfs_store_inode *inode;
  uint64_t logical;
  bool dirty;
  uint8_t bytes[NPFS_BLOCK_SIZE];
};

struct store_cache_chunk {
  struct store_cache_entry entries[STORE_CHUNK_ENTRIES];
};

struct store_metadata_chunk {
  uint8_t pages[STORE_METADATA_CHUNK_PAGES][NPFS_BLOCK_SIZE];
};

struct store_image {
  uint64_t home;
  uint32_t kind;
};

struct npfs_store_pool {
  struct npfs_store_pool *next;
  block_device_id device_id;
  struct npfs_ram *ram;
  struct gpt_partition partition;
  struct block_info device;
  struct npfs_header header;
  struct npfs_control control;
  unsigned control_slot;
  bool writable, degraded, failed, header_repair;
  enum call_status writeback_error;
  uint64_t free_blocks, next_free;
  struct npfs_volume catalog[NPFS_VOLUME_COUNT];
  struct npfs_store_volume *volumes;
  struct store_cache_chunk *cache[STORE_CACHE_CHUNKS];
  struct store_metadata_chunk *metadata_cache[STORE_METADATA_CHUNKS];
  uint64_t metadata_homes[STORE_METADATA_PAGES];
  unsigned metadata_next;
  struct store_image images[STORE_IMAGES_MAX];
  uint32_t image_count, image_capacity;
  /* Conservative free-block reservation until checkpoint; includes blocks
   * allocated and freed within this transaction. */
  uint64_t freed_pending;
  /* The running transaction collects whole operations until a commit. Images
   * staged before the current operation keep undo copies of its first edit, so
   * a failed operation leaves the earlier ones intact. */
  uint32_t operation_start, undo_count;
  uint32_t undo_index[STORE_OPERATION_IMAGES];
  uint64_t operation_freed_pending, operation_free_blocks, operation_next_free;
  uintptr_t scratch;
  size_t scratch_bytes;
  uintptr_t write_run;
  /* Retained mount storage; journal images overlay this durable bitmap base. */
  uintptr_t bitmap;
  size_t bitmap_bytes;
  bool bitmap_loaded;
  uint8_t io[2][NPFS_BLOCK_SIZE];
};

extern void npfs_require_worker(void);

static struct npfs_store_pool *opened_pools;
static uint64_t next_generation;

static enum call_status checkpoint(struct npfs_store_context *, struct npfs_store_pool *);
static enum call_status flush_files(struct npfs_store_context *, struct npfs_store_pool *);
static enum call_status cleanup_one(struct npfs_store_context *, struct npfs_store_volume *);
static enum call_status zero_growth_tail(struct npfs_store_context *, struct npfs_store_inode *);
static void discard_idle_inode(struct npfs_store_inode *, bool);

static void require_owner(void)
{
  npfs_require_worker();
}

static bool allocate_identity(uint64_t *identity)
{
  uint64_t flags = cpu_save_interrupts();
  bool allocated = fs_metadata_allocate_id(identity);
  cpu_restore_interrupts(flags);
  return allocated;
}

static void *store_allocate(size_t bytes)
{
  uint64_t flags = cpu_save_interrupts();
  void *pointer = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (pointer) {
    memset(pointer, 0, bytes);
  }
  return pointer;
}

static void store_free(void *pointer)
{
  uint64_t flags = cpu_save_interrupts();
  kfree(pointer);
  cpu_restore_interrupts(flags);
}

static enum call_status store_vm_allocate(size_t bytes, uintptr_t *address)
{
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_acquire(&owner)) {
    return CALL_BUSY;
  }
  uint64_t flags = cpu_save_interrupts();
  enum mm_result result = vm_alloc(vm_kernel_space(), bytes, PAGE_SIZE, PAGE_WRITE, address);
  vm_kernel_owner_release(&owner);
  cpu_restore_interrupts(flags);
  return result == MM_OK ? CALL_OK : result == MM_BUSY ? CALL_BUSY : CALL_NO_MEMORY;
}

static void store_vm_free(uintptr_t address, size_t bytes)
{
  if (!address) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  /* Store workers alone access these temporary/cache/pool mappings. Their
   * descriptors are detached before any next store turn can reach the range. */
  vm_kernel_retire(address, bytes);
  cpu_restore_interrupts(flags);
}

static enum call_status format_failure(struct npfs_store_context *context, enum npfs_status status)
{
  if (context->format_error == NPFS_OK) {
    context->format_error = status;
  }
  switch (status) {
    case NPFS_OK: return CALL_OK;
    case NPFS_INVALID: return CALL_BAD_REQUEST;
    case NPFS_NOT_FOUND: return CALL_NOT_FOUND;
    case NPFS_EXISTS: return CALL_ALREADY_EXISTS;
    case NPFS_NO_MEMORY: return CALL_NO_MEMORY;
    case NPFS_NO_SPACE: return CALL_NO_SPACE;
    case NPFS_UNSUPPORTED: return CALL_UNAVAILABLE;
    case NPFS_RECOVERY_REQUIRED: return CALL_READ_ONLY;
    default: return CALL_IO;
  }
}

static enum call_status corrupt(struct npfs_store_context *context)
{
  return format_failure(context, NPFS_CORRUPT);
}

static enum call_status backing_failure(struct npfs_store_context *context, enum block_result result)
{
  if (context->backing_error == BLOCK_OK) {
    context->backing_error = result;
  }
  return result == BLOCK_TIMED_OUT ? CALL_TIMED_OUT : CALL_IO;
}

static enum call_status transport(struct npfs_store_context *context, struct npfs_store_pool *pool,
  enum block_operation operation, uint64_t first, uint32_t count, void *bytes)
{
  require_owner();
  uint64_t ratio = NPFS_BLOCK_SIZE / pool->device.block_size;
  if (operation != BLOCK_FLUSH && (!count || first >= pool->header.pool_blocks ||
    count > pool->header.pool_blocks - first)) {
    return corrupt(context);
  }
  if (pool->ram) {
    return npfs_ram_io(pool->ram, operation, first, count, bytes, context->deadline);
  }
  uint64_t sector = operation == BLOCK_FLUSH ? 0 : pool->partition.first_block + first * ratio;
  uint32_t remaining = operation == BLOCK_FLUSH ? 0 : count * (uint32_t)ratio;
  uint8_t *cursor = bytes;
  for (;;) {
    if (task_deadline_expired(context->deadline)) {
      return backing_failure(context, BLOCK_TIMED_OUT);
    }
    uint32_t transfer = remaining;
    uint32_t maximum = pool->device.max_transfer / pool->device.block_size;
    if (transfer > maximum) {
      transfer = maximum;
    }
    struct block_ticket ticket;
    uint64_t flags = cpu_save_interrupts();
    enum block_result result = block_submit(pool->device_id, operation, sector, transfer,
      operation == BLOCK_WRITE ? cursor : NULL, &ticket);
    cpu_restore_interrupts(flags);
    if (result == BLOCK_FULL) {
      uint64_t retry = task_deadline_after_ms(1);
      kernel_task_sleep_until(retry < context->deadline ? retry : context->deadline);
      continue;
    }
    if (result != BLOCK_OK) {
      return backing_failure(context, result);
    }
    result = block_wait(&ticket, context->deadline);
    flags = cpu_save_interrupts();
    if (result != BLOCK_OK) {
      KASSERT(block_abandon(&ticket) == BLOCK_OK);
      cpu_restore_interrupts(flags);
      return backing_failure(context, result);
    }
    struct block_completion completion;
    size_t length = (size_t)transfer * pool->device.block_size;
    result = block_collect(&ticket, operation == BLOCK_READ ? cursor : NULL,
      operation == BLOCK_READ ? length : 0, &completion);
    KASSERT(result == BLOCK_OK);
    cpu_restore_interrupts(flags);
    if (completion.result != BLOCK_OK) {
      return backing_failure(context, completion.result);
    }
    if (operation != BLOCK_FLUSH && completion.bytes != length) {
      return backing_failure(context, BLOCK_IO_ERROR);
    }
    if (operation == BLOCK_FLUSH) {
      break;
    }
    cursor += length;
    sector += transfer;
    remaining -= transfer;
    if (!remaining) {
      break;
    }
  }
  return CALL_OK;
}

/* Scratch holds the images, the descriptor and control blocks, then the
 * current operation's undo copies. */
static uint8_t *image_bytes(struct npfs_store_pool *pool, unsigned index)
{
  return (uint8_t *)pool->scratch + (size_t)index * NPFS_BLOCK_SIZE;
}

static uint8_t *undo_bytes(struct npfs_store_pool *pool, unsigned slot)
{
  return image_bytes(pool, pool->image_capacity + 2 + slot);
}

static const uint8_t *bitmap_page(struct npfs_store_pool *pool, uint64_t page)
{
  KASSERT(pool->bitmap_loaded && page < pool->header.bitmap_blocks);
  uint64_t home = pool->header.bitmap_start + page;
  for (unsigned i = 0; i < pool->image_count; i++) {
    if (pool->images[i].home == home) {
      return image_bytes(pool, i);
    }
  }
  return (const uint8_t *)pool->bitmap + (size_t)page * NPFS_BLOCK_SIZE;
}

static const uint8_t *image_find(struct npfs_store_pool *pool, uint64_t home)
{
  for (unsigned i = 0; i < pool->image_count; i++) {
    if (pool->images[i].home == home) {
      return image_bytes(pool, i);
    }
  }
  return NULL;
}

static enum call_status read_block(struct npfs_store_context *context, struct npfs_store_pool *pool,
  uint64_t home, void *bytes)
{
  const uint8_t *image = image_find(pool, home);
  if (image) {
    memcpy(bytes, image, NPFS_BLOCK_SIZE);
    return CALL_OK;
  }
  if (pool->bitmap_loaded && home >= pool->header.bitmap_start &&
    home - pool->header.bitmap_start < pool->header.bitmap_blocks) {
    memcpy(bytes, (const uint8_t *)pool->bitmap +
      (size_t)(home - pool->header.bitmap_start) * NPFS_BLOCK_SIZE, NPFS_BLOCK_SIZE);
    return CALL_OK;
  }
  return transport(context, pool, BLOCK_READ, home, 1, bytes);
}

static uint8_t *metadata_cache_find(struct npfs_store_pool *pool, uint64_t home)
{
  KASSERT(home);
  for (unsigned i = 0; i < STORE_METADATA_PAGES; i++) {
    if (pool->metadata_homes[i] == home) {
      return pool->metadata_cache[i / STORE_METADATA_CHUNK_PAGES]->pages[
        i % STORE_METADATA_CHUNK_PAGES];
    }
  }
  return NULL;
}

static void metadata_cache_invalidate(struct npfs_store_pool *pool, uint64_t home)
{
  for (unsigned i = 0; i < STORE_METADATA_PAGES; i++) {
    if (pool->metadata_homes[i] == home) {
      pool->metadata_homes[i] = 0;
    }
  }
}

static void metadata_cache_store(struct npfs_store_pool *pool, uint64_t home, const void *bytes)
{
  for (unsigned attempt = 0; attempt < STORE_METADATA_CHUNKS; attempt++) {
    unsigned index = (pool->metadata_next + attempt * STORE_METADATA_CHUNK_PAGES) %
      STORE_METADATA_PAGES;
    unsigned chunk = index / STORE_METADATA_CHUNK_PAGES;
    if (!pool->metadata_cache[chunk]) {
      uintptr_t address;
      if (store_vm_allocate(sizeof(struct store_metadata_chunk), &address) != CALL_OK) {
        continue;
      }
      pool->metadata_cache[chunk] = (void *)address;
    }
    memcpy(pool->metadata_cache[chunk]->pages[index % STORE_METADATA_CHUNK_PAGES],
      bytes, NPFS_BLOCK_SIZE);
    pool->metadata_homes[index] = home;
    pool->metadata_next = (index + 1) % STORE_METADATA_PAGES;
    return;
  }
}

static const uint8_t *metadata_resident(struct npfs_store_pool *pool, uint64_t home)
{
  const uint8_t *image = image_find(pool, home);
  return image ? image : metadata_cache_find(pool, home);
}

static enum call_status read_metadata_block(struct npfs_store_context *context, struct npfs_store_pool *pool,
  uint64_t home, void *bytes)
{
  /* Cache only the durable base; transaction images always take precedence. */
  const uint8_t *cached = metadata_resident(pool, home);
  if (cached) {
    memcpy(bytes, cached, NPFS_BLOCK_SIZE);
    return CALL_OK;
  }
  enum call_status status = read_block(context, pool, home, bytes);
  if (status == CALL_OK) {
    metadata_cache_store(pool, home, bytes);
  }
  return status;
}

static enum call_status save_undo(struct npfs_store_pool *pool, unsigned index)
{
  for (unsigned i = 0; i < pool->undo_count; i++) {
    if (pool->undo_index[i] == index) {
      return CALL_OK;
    }
  }
  if (pool->undo_count == STORE_OPERATION_IMAGES) {
    return CALL_LIMIT;
  }
  memcpy(undo_bytes(pool, pool->undo_count), image_bytes(pool, index), NPFS_BLOCK_SIZE);
  pool->undo_index[pool->undo_count++] = index;
  return CALL_OK;
}

static enum call_status edit_block(struct npfs_store_context *context, struct npfs_store_pool *pool,
  uint64_t home, uint32_t kind, bool fresh, uint8_t **out)
{
  for (unsigned i = 0; i < pool->image_count; i++) {
    if (pool->images[i].home == home) {
      if (pool->images[i].kind != kind) {
        return corrupt(context);
      }
      if (i < pool->operation_start) {
        enum call_status status = save_undo(pool, i);
        if (status != CALL_OK) {
          return status;
        }
      }
      *out = image_bytes(pool, i);
      return CALL_OK;
    }
  }
  if (pool->image_count == pool->image_capacity) {
    return CALL_LIMIT;
  }
  unsigned index = pool->image_count;
  uint8_t *bytes = image_bytes(pool, index);
  enum call_status status = CALL_OK;
  if (fresh) {
    memset(bytes, 0, NPFS_BLOCK_SIZE);
  }
  else {
    if (kind == NPFS_METADATA_INODES || kind == NPFS_METADATA_DIRECTORY ||
      kind == NPFS_METADATA_INDIRECT) {
      status = read_metadata_block(context, pool, home, bytes);
    }
    else {
      status = read_block(context, pool, home, bytes);
    }
  }
  if (status != CALL_OK) {
    return status;
  }
  pool->images[index] = (struct store_image){home, kind};
  pool->image_count++;
  *out = bytes;
  return CALL_OK;
}

static bool bit_get(const uint8_t *bytes, uint64_t bit)
{
  return (bytes[bit / 8] & (1u << (bit % 8))) != 0;
}

static void bit_set(uint8_t *bytes, uint64_t bit, bool value)
{
  uint8_t mask = (uint8_t)(1u << (bit % 8));
  if (value) {
    bytes[bit / 8] |= mask;
  }
  else {
    bytes[bit / 8] &= (uint8_t)~mask;
  }
}

static bool block_allocated(struct npfs_store_pool *pool, uint64_t block)
{
  if (!npfs_data_block_valid(&pool->header, block)) {
    return false;
  }
  const uint8_t *bytes = bitmap_page(pool, block / NPFS_BITMAP_BITS);
  return bit_get(bytes, block % NPFS_BITMAP_BITS);
}

static enum call_status allocated_block(struct npfs_store_context *context, struct npfs_store_pool *pool,
  uint64_t block)
{
  return block_allocated(pool, block) ? CALL_OK : corrupt(context);
}

static enum call_status map_read(struct npfs_store_context *context, struct npfs_store_pool *pool,
  const uint64_t pointers[NPFS_POINTER_COUNT], uint64_t logical, uint64_t *physical)
{
  struct npfs_map_path path;
  enum npfs_status result = npfs_map_path(logical, &path);
  if (result != NPFS_OK) {
    return format_failure(context, result);
  }
  uint64_t block = pointers[path.slot];
  for (unsigned level = 0; block && level < path.depth; level++) {
    enum call_status status = allocated_block(context, pool, block);
    if (status != CALL_OK) {
      return status;
    }
    status = read_metadata_block(context, pool, block, pool->io[0]);
    if (status != CALL_OK) {
      return status;
    }
    block = npfs_get_u64(pool->io[0] + path.index[level] * 8);
  }
  if (block) {
    enum call_status status = allocated_block(context, pool, block);
    if (status != CALL_OK) {
      return status;
    }
  }
  *physical = block;
  return CALL_OK;
}

/* Lookahead never issues metadata I/O or reports a future mapping error. */
static bool map_read_resident(struct npfs_store_pool *pool,
  const uint64_t pointers[NPFS_POINTER_COUNT], uint64_t logical, uint64_t *physical)
{
  struct npfs_map_path path;
  if (npfs_map_path(logical, &path) != NPFS_OK) {
    return false;
  }
  uint64_t block = pointers[path.slot];
  for (unsigned level = 0; block && level < path.depth; level++) {
    if (!block_allocated(pool, block)) {
      return false;
    }
    const uint8_t *bytes = metadata_resident(pool, block);
    if (!bytes) {
      return false;
    }
    block = npfs_get_u64(bytes + path.index[level] * 8);
  }
  if (!block || !block_allocated(pool, block)) {
    return false;
  }
  *physical = block;
  return true;
}

static enum call_status bitmap_change(struct npfs_store_context *context, struct npfs_store_pool *pool,
  uint64_t block, bool allocated)
{
  if (!npfs_data_block_valid(&pool->header, block)) {
    return corrupt(context);
  }
  uint8_t *bytes;
  enum call_status status = edit_block(context, pool,
    pool->header.bitmap_start + block / NPFS_BITMAP_BITS, NPFS_METADATA_BITMAP, false, &bytes);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t bit = block % NPFS_BITMAP_BITS;
  if (bit_get(bytes, bit) == allocated) {
    return corrupt(context);
  }
  bit_set(bytes, bit, allocated);
  if (allocated) {
    if (!pool->free_blocks) {
      return corrupt(context);
    }
    pool->free_blocks--;
    /* A physical home may have belonged to metadata before its last free. */
    metadata_cache_invalidate(pool, block);
  } else {
    pool->free_blocks++;
    pool->freed_pending++;
  }
  return CALL_OK;
}

static enum call_status allocate_block(struct npfs_store_context *context, struct npfs_store_pool *pool,
  uint64_t *physical)
{
  /* Durable owners survive until checkpoint. Fresh metadata may be allocated
   * and freed in this transaction, so its staged image also blocks reuse. */
  if (pool->free_blocks == pool->freed_pending) {
    return CALL_NO_SPACE;
  }
  uint64_t starts[2] = {pool->next_free, 1};
  uint64_t ends[2] = {pool->header.pool_blocks, pool->next_free};
  for (unsigned range = 0; range < 2; range++) {
    uint64_t block = starts[range];
    while (block < ends[range]) {
      if (task_deadline_expired(context->deadline)) {
        return backing_failure(context, BLOCK_TIMED_OUT);
      }
      uint64_t page = block / NPFS_BITMAP_BITS;
      uint64_t page_base = page * NPFS_BITMAP_BITS;
      uint64_t end = page_base + NPFS_BITMAP_BITS;
      if (end > ends[range]) {
        end = ends[range];
      }
      const uint8_t *bytes = bitmap_page(pool, page);
      const uint8_t *durable = (const uint8_t *)pool->bitmap + (size_t)page * NPFS_BLOCK_SIZE;
      while (block < end) {
        unsigned bit = (unsigned)(block % 64);
        uint64_t word_base = block - bit;
        size_t word = (size_t)(word_base - page_base) / 8;
        uint64_t available = ~(npfs_get_u64(bytes + word) | npfs_get_u64(durable + word));
        available &= UINT64_MAX << bit;
        unsigned count = end - word_base < 64 ? (unsigned)(end - word_base) : 64;
        if (count < 64) {
          available &= (UINT64_C(1) << count) - 1;
        }
        while (available) {
          uint64_t candidate = word_base + (unsigned)__builtin_ctzll(available);
          if (image_find(pool, candidate)) {
            available &= available - 1;
            continue;
          }
          enum call_status status = bitmap_change(context, pool, candidate, true);
          if (status != CALL_OK) {
            return status;
          }
          pool->next_free = candidate + 1;
          if (pool->next_free == pool->header.pool_blocks) {
            pool->next_free = 1;
          }
          *physical = candidate;
          return CALL_OK;
        }
        block = word_base + count;
      }
    }
  }
  return CALL_NO_SPACE;
}

static enum call_status map_ensure(struct npfs_store_context *context, struct npfs_store_pool *pool,
  uint64_t pointers[NPFS_POINTER_COUNT], uint64_t logical, uint32_t kind,
  uint64_t *physical)
{
  struct npfs_map_path path;
  if (npfs_map_path(logical, &path) != NPFS_OK) {
    return CALL_FILE_TOO_LARGE;
  }
  uint64_t *root = &pointers[path.slot];
  uint64_t block = *root;
  uint8_t *parent = NULL;
  size_t parent_offset = 0;
  for (unsigned level = 0; level <= path.depth; level++) {
    if (!block) {
      enum call_status status = allocate_block(context, pool, &block);
      if (status != CALL_OK) {
        return status;
      }
      if (parent) {
        npfs_put_u64(parent + parent_offset, block);
      }
      else {
        *root = block;
      }
      if (level < path.depth || kind != 0) {
        uint8_t *fresh;
        status = edit_block(context, pool, block,
          level < path.depth ? NPFS_METADATA_INDIRECT : kind, true, &fresh);
        if (status != CALL_OK) {
          return status;
        }
      }
    }
    if (level == path.depth) {
      enum call_status status = allocated_block(context, pool, block);
      if (status != CALL_OK) {
        return status;
      }
      break;
    }
    enum call_status status = allocated_block(context, pool, block);
    if (status != CALL_OK) {
      return status;
    }
    status = edit_block(context, pool, block, NPFS_METADATA_INDIRECT, false, &parent);
    if (status != CALL_OK) {
      return status;
    }
    parent_offset = path.index[level] * 8;
    block = npfs_get_u64(parent + parent_offset);
  }
  *physical = block;
  return CALL_OK;
}

static enum call_status stage_volume(struct npfs_store_context *context, struct npfs_store_volume *volume)
{
  uint8_t *bytes;
  enum call_status status = edit_block(context, volume->pool,
    volume->pool->header.volume_start + volume->slot / 8, NPFS_METADATA_VOLUMES, false, &bytes);
  if (status != CALL_OK) {
    return status;
  }
  return format_failure(context, npfs_volume_encode(&volume->pool->header, &volume->record,
    bytes + (volume->slot % 8) * NPFS_VOLUME_SIZE));
}

static enum call_status stage_inode_record(struct npfs_store_context *context, struct npfs_store_inode *inode,
  const struct npfs_inode *record)
{
  struct npfs_store_volume *volume = inode->volume;
  struct npfs_store_pool *pool = volume->pool;
  uint64_t offset = inode->number * NPFS_INODE_SIZE;
  uint64_t block;
  enum call_status status = map_read(context, pool, volume->record.pointers,
    offset / NPFS_BLOCK_SIZE, &block);
  if (status != CALL_OK) {
    return status;
  }
  if (!block) {
    return corrupt(context);
  }
  uint8_t *bytes;
  status = edit_block(context, pool, block, NPFS_METADATA_INODES, false, &bytes);
  if (status != CALL_OK) {
    return status;
  }
  return format_failure(context, npfs_inode_encode(&pool->header, record,
    bytes + offset % NPFS_BLOCK_SIZE));
}

static enum call_status stage_inode(struct npfs_store_context *context, struct npfs_store_inode *inode)
{
  return stage_inode_record(context, inode, &inode->record);
}

static void touch_inode(struct npfs_store_context *context, struct npfs_inode *inode, bool created)
{
  inode->flags &= ~NPFS_TIME_MODIFIED_VALID;
  inode->modified_ns = 0;
  if (context->time_valid) {
    inode->flags |= NPFS_TIME_MODIFIED_VALID;
    inode->modified_ns = context->time_ns;
  }
  if (created) {
    inode->flags &= ~NPFS_TIME_CREATED_VALID;
    inode->created_ns = 0;
    if (context->time_valid) {
      inode->flags |= NPFS_TIME_CREATED_VALID;
      inode->created_ns = context->time_ns;
    }
  }
}

static enum call_status writable(struct npfs_store_pool *pool)
{
  if (pool->failed) {
    return pool->writeback_error;
  }
  return pool->writable ? CALL_OK : CALL_READ_ONLY;
}

static enum call_status latch_failure(struct npfs_store_pool *pool, enum call_status status)
{
  /* A terminal failure supersedes a retained recoverable error. */
  if (!pool->failed) {
    pool->writeback_error = status;
  }
  pool->failed = true;
  return status;
}

static enum call_status commit(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  if (!pool->image_count) {
    return CALL_OK;
  }
  if (pool->control.sequence > UINT64_MAX - 2) {
    return CALL_LIMIT;
  }
  struct npfs_control control = pool->control;
  control.sequence++;
  control.state = NPFS_JOURNAL_COMMITTED;
  control.image_count = pool->image_count;
  control.descriptor_blocks = (pool->image_count + NPFS_DESCRIPTORS_PER_BLOCK - 1) /
  NPFS_DESCRIPTORS_PER_BLOCK;
  uint8_t *descriptor_bytes = image_bytes(pool, pool->image_capacity);
  memset(descriptor_bytes, 0, NPFS_BLOCK_SIZE);
  for (unsigned i = 0; i < pool->image_count; i++) {
    struct npfs_descriptor descriptor = { .home = pool->images[i].home,
      .kind = pool->images[i].kind };
    enum npfs_status result = npfs_descriptor_encode(&pool->header, &descriptor,
      descriptor_bytes + i * NPFS_DESCRIPTOR_SIZE);
    if (result != NPFS_OK) {
      return format_failure(context, result);
    }
  }
  uint32_t crc = npfs_payload_begin(&control);
  crc = npfs_crc_update(crc, descriptor_bytes, NPFS_BLOCK_SIZE);
  for (unsigned i = 0; i < pool->image_count; i++) {
    crc = npfs_crc_update(crc, image_bytes(pool, i), NPFS_BLOCK_SIZE);
  }
  control.payload_crc = npfs_crc_finish(crc);
  enum call_status status = transport(context, pool, BLOCK_WRITE,
    pool->header.journal_start + 2, 1, descriptor_bytes);
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_WRITE, pool->header.journal_start + 3,
      pool->image_count, image_bytes(pool, 0));
  }
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  }
  if (status != CALL_OK) {
    return latch_failure(pool, status);
  }
  uint8_t *control_bytes = image_bytes(pool, pool->image_capacity + 1);
  enum npfs_status result = npfs_control_encode(&pool->header, &control, control_bytes);
  if (result != NPFS_OK) {
    return format_failure(context, result);
  }
  unsigned slot = pool->control_slot ^ 1;
  status = transport(context, pool, BLOCK_WRITE, pool->header.journal_start + slot, 1, control_bytes);
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  }
  if (status != CALL_OK) {
    return latch_failure(pool, CALL_OUTCOME_UNKNOWN);
  }
  pool->control = control;
  pool->control_slot = slot;
  return CALL_OK;
}

static enum call_status checkpoint(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  if (pool->control.state == NPFS_JOURNAL_EMPTY) {
    return CALL_OK;
  }
  if (pool->failed) {
    return pool->writeback_error;
  }
  if (pool->control.sequence == UINT64_MAX) {
    return latch_failure(pool, CALL_LIMIT);
  }
  for (unsigned i = 0; i < pool->image_count;) {
    unsigned count = 1;
    while (count < pool->image_count - i &&
      pool->images[i + count].home == pool->images[i].home + count) {
      count++;
    }
    enum call_status status = transport(context, pool, BLOCK_WRITE, pool->images[i].home,
      count, image_bytes(pool, i));
    if (status != CALL_OK) {
      return latch_failure(pool, status);
    }
    i += count;
  }
  enum call_status status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  if (status != CALL_OK) {
    return latch_failure(pool, status);
  }
  struct npfs_control empty = pool->control;
  empty.sequence++;
  empty.state = NPFS_JOURNAL_EMPTY;
  empty.image_count = empty.descriptor_blocks = empty.payload_crc = 0;
  if (npfs_control_encode(&pool->header, &empty, pool->io[0]) != NPFS_OK) {
    return corrupt(context);
  }
  unsigned slot = pool->control_slot ^ 1;
  status = transport(context, pool, BLOCK_WRITE, pool->header.journal_start + slot, 1, pool->io[0]);
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  }
  if (status != CALL_OK) {
    return latch_failure(pool, status);
  }
  pool->control = empty;
  pool->control_slot = slot;
  /* Reuse becomes safe only after the newer EMPTY control is durable. */
  for (unsigned i = 0; i < pool->image_count; i++) {
    metadata_cache_invalidate(pool, pool->images[i].home);
    if (pool->images[i].kind == NPFS_METADATA_BITMAP) {
      KASSERT(pool->bitmap_loaded);
      memcpy((uint8_t *)pool->bitmap +
        (size_t)(pool->images[i].home - pool->header.bitmap_start) * NPFS_BLOCK_SIZE,
        image_bytes(pool, i), NPFS_BLOCK_SIZE);
    }
  }
  pool->image_count = 0;
  pool->freed_pending = 0;
  return CALL_OK;
}

/* Publishes staged operations that are not committed yet. A committed journal
 * holds no uncommitted images: staging checkpoints it first. */
static enum call_status commit_running(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  if (pool->control.state != NPFS_JOURNAL_EMPTY) {
    return CALL_OK;
  }
  return commit(context, pool);
}

static bool allocation_short(const struct npfs_store_pool *pool, unsigned blocks)
{
  return pool->freed_pending && pool->free_blocks - pool->freed_pending < blocks;
}

/* Starts one operation in the running transaction, after committing and
 * checkpointing that transaction when it lacks room for IMAGES more images, or
 * when fewer than BLOCKS blocks are allocatable but a checkpoint would release
 * blocks it freed. Every staging path begins an operation, so one left open by
 * an early return before staging is replaced. */
static enum call_status operation_begin(struct npfs_store_context *context, struct npfs_store_pool *pool,
  unsigned images, unsigned blocks)
{
  enum call_status status = writable(pool);
  if (status == CALL_OK) {
    status = checkpoint(context, pool);
  }
  if (status == CALL_OK && (pool->image_capacity - pool->image_count < images ||
    allocation_short(pool, blocks))) {
    status = commit(context, pool);
    if (status == CALL_OK) {
      status = checkpoint(context, pool);
    }
  }
  if (status != CALL_OK) {
    return status;
  }
  pool->operation_start = pool->image_count;
  pool->undo_count = 0;
  pool->operation_freed_pending = pool->freed_pending;
  pool->operation_free_blocks = pool->free_blocks;
  pool->operation_next_free = pool->next_free;
  return CALL_OK;
}

static void operation_end(struct npfs_store_pool *pool)
{
  pool->operation_start = 0;
  pool->undo_count = 0;
}

/* Drops the failed operation's images, keeping the transaction's earlier ones.
 * A terminal failure keeps everything for diagnosis; nothing commits after it. */
static void operation_abort(struct npfs_store_pool *pool)
{
  if (!pool->failed && pool->control.state == NPFS_JOURNAL_EMPTY) {
    for (unsigned i = 0; i < pool->undo_count; i++) {
      memcpy(image_bytes(pool, pool->undo_index[i]), undo_bytes(pool, i), NPFS_BLOCK_SIZE);
    }
    pool->image_count = pool->operation_start;
    pool->freed_pending = pool->operation_freed_pending;
    pool->free_blocks = pool->operation_free_blocks;
    pool->next_free = pool->operation_next_free;
  }
  operation_end(pool);
}

static enum call_status validate_log_image(struct npfs_store_context *context, struct npfs_store_pool *pool,
  const struct npfs_descriptor *descriptor, const uint8_t *bytes)
{
  if (descriptor->kind == NPFS_METADATA_BITMAP) {
    uint64_t base = (descriptor->home - pool->header.bitmap_start) * NPFS_BITMAP_BITS;
    for (unsigned bit = 0; bit < NPFS_BITMAP_BITS; bit++) {
      uint64_t number = base + bit;
      if ((number >= pool->header.pool_blocks || !npfs_data_block_valid(&pool->header, number)) &&
        !bit_get(bytes, bit)) {
        return corrupt(context);
      }
    }
  } else if (descriptor->kind == NPFS_METADATA_VOLUMES) {
    for (unsigned i = 0; i < 8; i++) {
      struct npfs_volume volume;
      enum npfs_status status = npfs_volume_decode(&pool->header, bytes + i * NPFS_VOLUME_SIZE, &volume);
      if (status != NPFS_OK) {
        return format_failure(context, status);
      }
    }
  } else if (descriptor->kind == NPFS_METADATA_INODES) {
    for (unsigned i = 0; i < 16; i++) {
      struct npfs_inode inode;
      enum npfs_status status = npfs_inode_decode(&pool->header, bytes + i * NPFS_INODE_SIZE, &inode);
      if (status != NPFS_OK) {
        return format_failure(context, status);
      }
    }
  } else if (descriptor->kind == NPFS_METADATA_DIRECTORY) {
    size_t offset = 0;
    while (offset < NPFS_BLOCK_SIZE) {
      struct npfs_dirent entry;
      enum npfs_status status = npfs_dirent_decode(&pool->header, bytes + offset,
        NPFS_BLOCK_SIZE - offset, &entry);
      if (status != NPFS_OK) {
        return format_failure(context, status);
      }
      offset += entry.record_length;
    }
  } else if (descriptor->kind == NPFS_METADATA_INDIRECT) {
    for (unsigned i = 0; i < NPFS_INDIRECT_COUNT; i++) {
      uint64_t pointer = npfs_get_u64(bytes + i * 8);
      if (pointer && !npfs_data_block_valid(&pool->header, pointer)) {
        return corrupt(context);
      }
    }
  }
  return CALL_OK;
}

static enum call_status replay(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  if (pool->control.state == NPFS_JOURNAL_EMPTY) {
    return CALL_OK;
  }
  if (!pool->writable) {
    kwarn("npfs: journal replay required; mount read-write once to recover\n");
    return format_failure(context, NPFS_RECOVERY_REQUIRED);
  }
  if (pool->control.sequence == UINT64_MAX) {
    return CALL_LIMIT;
  }
  uint64_t payload_blocks = (uint64_t)pool->control.descriptor_blocks + pool->control.image_count;
  if (payload_blocks > SIZE_MAX / NPFS_BLOCK_SIZE) {
    return CALL_NO_MEMORY;
  }
  size_t payload_bytes = (size_t)payload_blocks * NPFS_BLOCK_SIZE;
  size_t target_bytes = (size_t)(pool->header.pool_blocks / 8 + (pool->header.pool_blocks % 8 != 0));
  uintptr_t payload_address = 0, targets_address = 0;
  enum call_status status = store_vm_allocate(payload_bytes, &payload_address);
  if (status != CALL_OK) {
    return status;
  }
  status = store_vm_allocate(target_bytes, &targets_address);
  if (status != CALL_OK) {
    goto done;
  }
  uint8_t *payload = (void *)payload_address;
  uint8_t *targets = (void *)targets_address;
  uint32_t crc = npfs_payload_begin(&pool->control);
  for (uint64_t i = 0; i < payload_blocks; i++) {
    status = transport(context, pool, BLOCK_READ, pool->header.journal_start + 2 + i,
      1, payload + i * NPFS_BLOCK_SIZE);
    if (status != CALL_OK) {
      goto done;
    }
    crc = npfs_crc_update(crc, payload + i * NPFS_BLOCK_SIZE, NPFS_BLOCK_SIZE);
  }
  if (npfs_crc_finish(crc) != pool->control.payload_crc) {
    status = corrupt(context);
    goto done;
  }
  size_t descriptors_bytes = (size_t)pool->control.descriptor_blocks * NPFS_BLOCK_SIZE;
  for (unsigned i = 0; i < pool->control.image_count; i++) {
    struct npfs_descriptor descriptor;
    enum npfs_status result = npfs_descriptor_decode(&pool->header,
      payload + (size_t)i * NPFS_DESCRIPTOR_SIZE, &descriptor);
    if (result != NPFS_OK) {
      status = format_failure(context, result);
      goto done;
    }
    if (bit_get(targets, descriptor.home)) {
      status = corrupt(context);
      goto done;
    }
    bit_set(targets, descriptor.home, true);
    status = validate_log_image(context, pool, &descriptor,
      payload + descriptors_bytes + (size_t)i * NPFS_BLOCK_SIZE);
    if (status != CALL_OK) {
      goto done;
    }
  }
  for (size_t i = (size_t)pool->control.image_count * NPFS_DESCRIPTOR_SIZE;
    i < descriptors_bytes; i++) {
    if (payload[i]) {
      status = corrupt(context);
      goto done;
    }
  }
  for (unsigned i = 0; i < pool->control.image_count; i++) {
    uint64_t home = npfs_get_u64(payload + (size_t)i * NPFS_DESCRIPTOR_SIZE);
    status = transport(context, pool, BLOCK_WRITE, home, 1,
      payload + descriptors_bytes + (size_t)i * NPFS_BLOCK_SIZE);
    if (status != CALL_OK) {
      goto done;
    }
  }
  status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  if (status != CALL_OK) {
    goto done;
  }
  struct npfs_control empty = pool->control;
  empty.state = NPFS_JOURNAL_EMPTY;
  empty.sequence++;
  empty.image_count = empty.descriptor_blocks = empty.payload_crc = 0;
  if (npfs_control_encode(&pool->header, &empty, pool->io[0]) != NPFS_OK) {
    status = corrupt(context);
    goto done;
  }
  unsigned slot = pool->control_slot ^ 1;
  status = transport(context, pool, BLOCK_WRITE, pool->header.journal_start + slot, 1, pool->io[0]);
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  }
  if (status == CALL_OK) {
    kinfo("npfs: replayed journal sequence %llu (%u blocks)\n",
      (unsigned long long)pool->control.sequence, pool->control.image_count);
    pool->control = empty;
    pool->control_slot = slot;
  }
  done:
  store_vm_free(targets_address, target_bytes);
  store_vm_free(payload_address, payload_bytes);
  return status;
}

static enum call_status read_headers(struct npfs_store_context *context,
  struct npfs_store_pool *pool, struct npfs_header_selection *selection)
{
  enum call_status status = transport(context, pool, BLOCK_READ, 0, 1, pool->io[0]);
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_READ, pool->header.pool_blocks - 1, 1, pool->io[1]);
  }
  return status == CALL_OK ? format_failure(context, npfs_header_select(pool->io[0],
    pool->io[1], pool->header.pool_blocks, selection)) : status;
}

static enum call_status repair_header(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  struct npfs_header_selection selection;
  enum call_status status = read_headers(context, pool, &selection);
  if (status != CALL_OK) {
    return status;
  }
  if (memcmp(&selection.header, &pool->header, sizeof(pool->header))) {
    return corrupt(context);
  }
  if (selection.feature_gain) {
    uint64_t block = selection.slot == 0 ? pool->header.pool_blocks - 1 : 0;
    status = transport(context, pool, BLOCK_WRITE, block, 1, pool->io[selection.slot]);
    if (status == CALL_OK) {
      status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
    }
    if (status != CALL_OK) {
      return latch_failure(pool, status);
    }
  }
  pool->header_repair = false;
  pool->degraded = selection.degraded;
  return CALL_OK;
}

static enum call_status prepare_writable(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  if (pool->failed) {
    return pool->writeback_error;
  }
  enum call_status status = format_failure(context, npfs_features_check_supported(&pool->header,
    true, NPFS_FEATURE_RO_HARD_LINKS));
  if (status != CALL_OK) {
    return status;
  }
  if (!pool->device.writable || !pool->device.flush_supported || pool->device.write_failed) {
    return CALL_READ_ONLY;
  }
  uint64_t capacity = npfs_journal_capacity(pool->header.journal_blocks);
  if (capacity < MOUNT_NPFS_MIN_JOURNAL_IMAGES) {
    return CALL_LIMIT;
  }
  pool->image_capacity = capacity < STORE_IMAGES_MAX ? (unsigned)capacity : STORE_IMAGES_MAX;
  pool->scratch_bytes = ((size_t)pool->image_capacity + 2 + STORE_OPERATION_IMAGES) *
    NPFS_BLOCK_SIZE;
  status = store_vm_allocate(pool->scratch_bytes, &pool->scratch);
  if (status == CALL_OK && pool->header_repair) {
    status = repair_header(context, pool);
  }
  if (status == CALL_OK) {
    pool->writable = true;
  } else {
    store_vm_free(pool->scratch, pool->scratch_bytes);
    pool->scratch = 0;
    pool->scratch_bytes = 0;
    pool->image_capacity = 0;
  }
  return status;
}

static enum call_status enable_hard_links(struct npfs_store_context *context,
  struct npfs_store_pool *pool)
{
  if (pool->header.read_only_compatible & NPFS_FEATURE_RO_HARD_LINKS) {
    return CALL_OK;
  }
  struct npfs_header_selection selection;
  enum call_status status = read_headers(context, pool, &selection);
  if (status != CALL_OK) {
    return status;
  }
  if (memcmp(&selection.header, &pool->header, sizeof(pool->header))) {
    return latch_failure(pool, corrupt(context));
  }
  unsigned first = selection.slot ^ 1;
  status = format_failure(context, npfs_header_gain_features(pool->io[selection.slot],
    0, NPFS_FEATURE_RO_HARD_LINKS, 0, pool->io[first]));
  if (status != CALL_OK) {
    return status;
  }
  /* Preserve the selected valid copy until the other copy has been flushed,
   * including on a degraded pool. Feature gain is permanent, outside undo. */
  status = transport(context, pool, BLOCK_WRITE,
    first == 0 ? 0 : pool->header.pool_blocks - 1, 1, pool->io[first]);
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  }
  if (status == CALL_OK) {
    pool->header.read_only_compatible |= NPFS_FEATURE_RO_HARD_LINKS;
    status = transport(context, pool, BLOCK_WRITE,
      selection.slot == 0 ? 0 : pool->header.pool_blocks - 1, 1, pool->io[first]);
  }
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  }
  if (status != CALL_OK) {
    return latch_failure(pool, status);
  }
  pool->degraded = false;
  pool->header_repair = false;
  return CALL_OK;
}

static enum call_status open_backing(struct npfs_store_context *context,
  block_device_id device_id, const struct gpt_partition *partition, const struct block_info *device,
  bool write, struct npfs_ram *ram, struct npfs_store_pool **out)
{
  require_owner();
  *out = NULL;
  if ((device->block_size != 512 && device->block_size != NPFS_BLOCK_SIZE) ||
    device->max_transfer < device->block_size || !partition->block_count ||
    partition->first_block >= device->block_count ||
    partition->block_count > device->block_count - partition->first_block) {
    return CALL_UNAVAILABLE;
  }
  struct npfs_store_pool *pool = store_allocate(sizeof(*pool));
  if (!pool) {
    return CALL_NO_MEMORY;
  }
  pool->device_id = device_id;
  pool->ram = ram;
  pool->partition = *partition;
  pool->device = *device;
  pool->next_free = 1;
  pool->header.pool_blocks = partition->block_count / (NPFS_BLOCK_SIZE / device->block_size);
  enum call_status status = CALL_IO;
  if (pool->header.pool_blocks < 2 || pool->header.pool_blocks > UINT64_MAX / NPFS_BLOCK_SIZE) {
    goto fail;
  }
  struct npfs_header_selection selection;
  status = read_headers(context, pool, &selection);
  if (status != CALL_OK) {
    goto fail;
  }
  pool->header = selection.header;
  pool->degraded = selection.degraded;
  pool->header_repair = selection.feature_gain;
  status = format_failure(context, npfs_features_check_supported(&pool->header, write,
    NPFS_FEATURE_RO_HARD_LINKS));
  if (status != CALL_OK) {
    goto fail;
  }
  for (struct npfs_store_pool *other = opened_pools; other; other = other->next) {
    if (!memcmp(pool->header.pool_id, other->header.pool_id, NPFS_ID_SIZE)) {
      status = CALL_ALREADY_EXISTS;
      goto fail;
    }
  }
  struct npfs_control controls[2];
  enum npfs_status states[2];
  for (unsigned i = 0; i < 2; i++) {
    status = transport(context, pool, BLOCK_READ, pool->header.journal_start + i, 1, pool->io[i]);
    if (status != CALL_OK) {
      goto fail;
    }
    states[i] = npfs_control_decode(&pool->header, pool->io[i], &controls[i]);
    if (states[i] != NPFS_OK && npfs_control_checksum_valid(pool->io[i])) {
      status = format_failure(context, states[i]);
      goto fail;
    }
  }
  if ((states[0] != NPFS_OK && states[1] != NPFS_OK) ||
    (states[0] == NPFS_OK && states[1] == NPFS_OK && controls[0].sequence == controls[1].sequence &&
    memcmp(pool->io[0], pool->io[1], NPFS_BLOCK_SIZE))) {
    status = corrupt(context);
    goto fail;
  }
  pool->control_slot = states[0] != NPFS_OK ? 1 :
  states[1] != NPFS_OK ? 0 : controls[1].sequence > controls[0].sequence ? 1 : 0;
  pool->control = controls[pool->control_slot];
  if (write) {
    status = prepare_writable(context, pool);
    if (status != CALL_OK) {
      goto fail;
    }
  }
  status = replay(context, pool);
  if (status != CALL_OK) {
    goto fail;
  }
  for (unsigned page = 0; page < NPFS_VOLUME_TABLE_BLOCKS; page++) {
    status = read_block(context, pool, pool->header.volume_start + page, pool->io[0]);
    if (status != CALL_OK) {
      goto fail;
    }
    for (unsigned i = 0; i < 8; i++) {
      unsigned slot = page * 8 + i;
      enum npfs_status result = npfs_volume_decode(&pool->header,
        pool->io[0] + i * NPFS_VOLUME_SIZE, &pool->catalog[slot]);
      if (result != NPFS_OK) {
        status = format_failure(context, result);
        goto fail;
      }
      if (pool->catalog[slot].state != NPFS_VOLUME_LIVE) {
        continue;
      }
      for (unsigned j = 0; j < slot; j++) {
        struct npfs_volume *a = &pool->catalog[slot], *b = &pool->catalog[j];
        if (b->state == NPFS_VOLUME_LIVE && (!memcmp(a->id, b->id, NPFS_ID_SIZE) ||
          (a->name_length == b->name_length && !memcmp(a->name, b->name, a->name_length)))) {
          status = corrupt(context);
          goto fail;
        }
      }
    }
  }
  if (pool->header.bitmap_blocks > SIZE_MAX / NPFS_BLOCK_SIZE) {
    status = CALL_LIMIT;
    goto fail;
  }
  pool->bitmap_bytes = (size_t)pool->header.bitmap_blocks * NPFS_BLOCK_SIZE;
  status = store_vm_allocate(pool->bitmap_bytes, &pool->bitmap);
  if (status != CALL_OK) {
    goto fail;
  }
  for (uint64_t page = 0; page < pool->header.bitmap_blocks;) {
    uint64_t remaining = pool->header.bitmap_blocks - page;
    uint32_t count = remaining < STORE_BITMAP_READ_BLOCKS ? (uint32_t)remaining :
      STORE_BITMAP_READ_BLOCKS;
    status = transport(context, pool, BLOCK_READ, pool->header.bitmap_start + page,
      count, (uint8_t *)pool->bitmap + (size_t)page * NPFS_BLOCK_SIZE);
    if (status != CALL_OK) {
      goto fail;
    }
    page += count;
  }
  for (uint64_t page = 0; page < pool->header.bitmap_blocks; page++) {
    if (task_deadline_expired(context->deadline)) {
      status = backing_failure(context, BLOCK_TIMED_OUT);
      goto fail;
    }
    const uint8_t *bytes = (const uint8_t *)pool->bitmap + (size_t)page * NPFS_BLOCK_SIZE;
    for (unsigned bit = 0; bit < NPFS_BITMAP_BITS; bit++) {
      uint64_t number = page * NPFS_BITMAP_BITS + bit;
      bool fixed = number >= pool->header.pool_blocks || !npfs_data_block_valid(&pool->header, number);
      bool allocated = bit_get(bytes, bit);
      if (fixed && !allocated) {
        status = corrupt(context);
        goto fail;
      }
      if (!fixed && !allocated) {
        if (!pool->free_blocks) {
          pool->next_free = number;
        }
        pool->free_blocks++;
      }
    }
  }
  pool->bitmap_loaded = true;
  pool->next = opened_pools;
  opened_pools = pool;
  *out = pool;
  return CALL_OK;
  fail:
  store_vm_free(pool->bitmap, pool->bitmap_bytes);
  store_vm_free(pool->scratch, pool->scratch_bytes);
  store_free(pool);
  return status;
}

enum call_status npfs_store_open(struct npfs_store_context *context,
  block_device_id device_id, const struct gpt_partition *partition, const struct block_info *device,
  bool writable, struct npfs_store_pool **out)
{
  return open_backing(context, device_id, partition, device, writable, NULL, out);
}

enum call_status npfs_store_open_ram(struct npfs_store_context *context,
    uint64_t capacity, struct npfs_store_pool **out)
{
  struct npfs_ram *ram;
  enum call_status status = npfs_ram_create(capacity, context->deadline, &ram);
  if (status != CALL_OK) return status;
  struct block_info device = {.block_size = NPFS_BLOCK_SIZE,
    .block_count = capacity / NPFS_BLOCK_SIZE, .max_transfer = 1024 * 1024,
    .request_slots = 1, .writable = true, .flush_supported = true};
  struct gpt_partition partition = {.block_count = device.block_count};
  status = open_backing(context, BLOCK_DEVICE_ID_NONE, &partition, &device, true, ram, out);
  if (status != CALL_OK) npfs_ram_discard(ram);
  return status;
}

enum call_status npfs_store_upgrade(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  require_owner();
  if (pool->writable) {
    return writable(pool);
  }
  return prepare_writable(context, pool);
}

const uint8_t *npfs_store_pool_id(const struct npfs_store_pool *pool)
{
  return pool->header.pool_id;
}

bool npfs_store_writable(const struct npfs_store_pool *pool)
{
  return pool->writable && !pool->failed;
}

static enum call_status inode_record_read(struct npfs_store_context *context, struct npfs_store_volume *volume,
  uint64_t number, struct npfs_inode *record)
{
  if (number >= volume->record.inode_bytes / NPFS_INODE_SIZE) {
    return corrupt(context);
  }
  uint64_t offset = number * NPFS_INODE_SIZE;
  uint64_t block;
  enum call_status status = map_read(context, volume->pool, volume->record.pointers,
    offset / NPFS_BLOCK_SIZE, &block);
  if (status != CALL_OK) {
    return status;
  }
  if (!block) {
    return corrupt(context);
  }
  status = read_metadata_block(context, volume->pool, block, volume->pool->io[0]);
  if (status != CALL_OK) {
    return status;
  }
  return format_failure(context, npfs_inode_decode(&volume->pool->header,
    volume->pool->io[0] + offset % NPFS_BLOCK_SIZE, record));
}

static enum call_status inode_get(struct npfs_store_context *context, struct npfs_store_volume *volume,
  uint64_t number, struct npfs_store_inode **out)
{
  for (struct npfs_store_inode *inode = volume->inodes; inode; inode = inode->next) {
    if (inode->number == number) {
      *out = inode;
      return CALL_OK;
    }
  }
  struct npfs_store_inode *inode = store_allocate(sizeof(*inode));
  if (!inode) {
    return CALL_NO_MEMORY;
  }
  enum call_status status = inode_record_read(context, volume, number, &inode->record);
  if (status != CALL_OK) {
    store_free(inode);
    return status;
  }
  if (next_generation == UINT64_MAX || !allocate_identity(&inode->identity)) {
    store_free(inode);
    return CALL_LIMIT;
  }
  inode->volume = volume;
  inode->number = number;
  inode->durable = inode->record;
  inode->durable_size = inode->record.size;
  inode->generation = ++next_generation;
  inode->next = volume->inodes;
  volume->inodes = inode;
  *out = inode;
  return CALL_OK;
}

static void free_volume_storage(struct npfs_store_volume *volume)
{
  while (volume->free_slots) {
    struct store_free_slot *next = volume->free_slots->next;
    if (volume->free_slots->heap_owned) {
      store_free(volume->free_slots);
    }
    volume->free_slots = next;
  }
  while (volume->inodes) {
    struct npfs_store_inode *next = volume->inodes->next;
    store_free(volume->inodes);
    volume->inodes = next;
  }
  store_free(volume);
}

static enum call_status volume_mount(struct npfs_store_context *context, struct npfs_store_pool *pool,
  unsigned slot, struct npfs_store_volume **out)
{
  for (struct npfs_store_volume *volume = pool->volumes; volume; volume = volume->next) {
    if (volume->slot == slot) {
      *out = volume;
      return CALL_OK;
    }
  }
  struct npfs_store_volume *volume = store_allocate(sizeof(*volume));
  if (!volume) {
    return CALL_NO_MEMORY;
  }
  if (!allocate_identity(&volume->domain)) {
    store_free(volume);
    return CALL_LIMIT;
  }
  volume->pool = pool;
  volume->slot = slot;
  volume->record = pool->catalog[slot];
  enum call_status status = CALL_OK;
  uint64_t count = volume->record.inode_bytes / NPFS_INODE_SIZE;
  uint64_t cleanup_count = 0;
  for (uint64_t number = 0; number < count; number++) {
    struct npfs_inode record;
    status = inode_record_read(context, volume, number, &record);
    if (status != CALL_OK) {
      goto fail;
    }
    if ((!number && record.kind != NPFS_INODE_FREE) ||
      (number == 1 && (record.kind != NPFS_INODE_DIRECTORY || record.parent != 1 || (record.cleanup & NPFS_CLEANUP_DETACHED)))) {
      status = corrupt(context);
      goto fail;
    }
    if (record.cleanup) {
      cleanup_count++;
    }
    if (number && record.kind == NPFS_INODE_FREE) {
      struct store_free_slot *free_slot = store_allocate(sizeof(*free_slot));
      if (!free_slot) {
        status = CALL_NO_MEMORY;
        goto fail;
      }
      free_slot->heap_owned = true;
      free_slot->number = number;
      free_slot->next = volume->free_slots;
      volume->free_slots = free_slot;
    }
  }
  uint64_t cleanup = volume->record.cleanup_head;
  uint64_t walked = 0;
  while (cleanup) {
    struct npfs_store_inode *inode;
    if (++walked >= count) {
      status = corrupt(context);
      goto fail;
    }
    status = inode_get(context, volume, cleanup, &inode);
    if (status != CALL_OK) {
      goto fail;
    }
    if (!inode->record.cleanup) {
      status = corrupt(context);
      goto fail;
    }
    cleanup = inode->record.cleanup_next;
  }
  if (walked != cleanup_count) {
    status = corrupt(context);
    goto fail;
  }
  volume->next = pool->volumes;
  pool->volumes = volume;
  *out = volume;
  return CALL_OK;
  fail:
  free_volume_storage(volume);
  return status;
}

enum call_status npfs_store_root(struct npfs_store_context *context, struct npfs_store_pool *pool,
  const char *name, size_t length, struct npfs_store_inode **out)
{
  require_owner();
  *out = NULL;
  for (unsigned slot = 0; slot < NPFS_VOLUME_COUNT; slot++) {
    struct npfs_volume *record = &pool->catalog[slot];
    if (record->state != NPFS_VOLUME_LIVE || record->name_length != length ||
      memcmp(record->name, name, length)) {
      continue;
    }
    struct npfs_store_volume *volume;
    enum call_status status = volume_mount(context, pool, slot, &volume);
    if (status != CALL_OK) {
      return status;
    }
    status = inode_get(context, volume, 1, out);
    if (status == CALL_OK) {
      npfs_store_retain(*out);
    }
    return status;
  }
  return CALL_NOT_FOUND;
}

void npfs_store_retain(struct npfs_store_inode *inode)
{
  require_owner();
  KASSERT(inode->references != SIZE_MAX);
  inode->references++;
}

void npfs_store_release(struct npfs_store_inode *inode)
{
  require_owner();
  KASSERT(inode->references);
  inode->references--;
  discard_idle_inode(inode, false);
}

uint64_t npfs_store_kind(const struct npfs_store_inode *inode)
{
  return inode->record.kind == NPFS_INODE_DIRECTORY ? DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE;
}

uint64_t npfs_store_size(const struct npfs_store_inode *inode)
{
  return inode->record.size;
}

void npfs_store_file_info(struct npfs_store_inode *inode, struct file_info_reply *info)
{
  require_owner();
  *info = (struct file_info_reply){
    .valid = FILE_INFO_SIZE_VALID | FILE_INFO_DOMAIN_VALID | FILE_INFO_OBJECT_VALID,
    .size = inode->record.size,
    .domain = inode->volume->domain,
    .object = inode->identity,
  };
  if (inode->record.kind == NPFS_INODE_FILE) {
    info->valid |= FILE_INFO_LINK_COUNT_VALID;
    info->link_count = inode->record.link_count;
  }
  if (inode->record.flags & NPFS_TIME_MODIFIED_VALID) {
    fs_metadata_mtime(inode->record.modified_ns, info);
  }
}

bool npfs_store_same_volume(const struct npfs_store_inode *a, const struct npfs_store_inode *b)
{
  return a->volume == b->volume;
}

bool npfs_store_is_root(const struct npfs_store_inode *inode)
{
  return inode->number == inode->volume->record.root_inode;
}

struct npfs_store_pool *npfs_store_inode_pool(struct npfs_store_inode *inode)
{
  return inode->volume->pool;
}

/* One uninterrupted scan owns io[0] until it returns or starts an inode read
 * or mutation. Decoded entries own their names; the scratch page never escapes. */
struct directory_walk {
  uint64_t logical;
  bool loaded;
};

static enum call_status directory_record(struct npfs_store_context *context, struct npfs_store_inode *directory,
  struct directory_walk *walk, uint64_t position, struct npfs_dirent *entry)
{
  struct npfs_store_pool *pool = directory->volume->pool;
  uint64_t logical = position / NPFS_BLOCK_SIZE;
  if (!walk->loaded || walk->logical != logical) {
    uint64_t physical;
    enum call_status status = map_read(context, pool, directory->record.pointers,
      logical, &physical);
    if (status != CALL_OK) {
      return status;
    }
    if (!physical) {
      return corrupt(context);
    }
    status = read_metadata_block(context, pool, physical, pool->io[0]);
    if (status != CALL_OK) {
      return status;
    }
    walk->logical = logical;
    walk->loaded = true;
  }
  unsigned offset = (unsigned)(position % NPFS_BLOCK_SIZE);
  return format_failure(context, npfs_dirent_decode(&pool->header, pool->io[0] + offset,
    NPFS_BLOCK_SIZE - offset, entry));
}

static enum call_status directory_find(struct npfs_store_context *context, struct npfs_store_inode *directory,
  const char *name, size_t length, struct npfs_dirent *found, uint64_t *position)
{
  if (directory->record.kind != NPFS_INODE_DIRECTORY) {
    return CALL_WRONG_TYPE;
  }
  if (!npfs_name_valid((const uint8_t *)name, length)) {
    return CALL_BAD_REQUEST;
  }
  bool match = false;
  struct directory_walk walk = {0};
  uint64_t offset = 0;
  while (offset < directory->record.size) {
    struct npfs_dirent entry;
    enum call_status status = directory_record(context, directory, &walk, offset, &entry);
    if (status != CALL_OK) {
      return status;
    }
    if (entry.inode && entry.name_length == length && !memcmp(entry.name, name, length)) {
      if (match) {
        return corrupt(context);
      }
      *found = entry;
      *position = offset;
      match = true;
    }
    offset += entry.record_length;
  }
  return match ? CALL_OK : CALL_NOT_FOUND;
}

static bool valid_child(const struct npfs_store_inode *directory,
  const struct npfs_store_inode *child)
{
  return child->number != 1 && child->record.kind != NPFS_INODE_FREE &&
    !(child->record.cleanup & NPFS_CLEANUP_DETACHED) &&
    (child->record.kind != NPFS_INODE_FILE || child->record.link_count != 0) &&
    (child->record.kind != NPFS_INODE_DIRECTORY || child->record.parent == directory->number);
}

enum call_status npfs_store_lookup(struct npfs_store_context *context, struct npfs_store_inode *directory,
  const char *name, size_t length, struct npfs_store_inode **out)
{
  require_owner();
  *out = NULL;
  struct npfs_dirent entry;
  uint64_t position;
  enum call_status status = directory_find(context, directory, name, length, &entry, &position);
  if (status != CALL_OK) {
    return status;
  }
  status = inode_get(context, directory->volume, entry.inode, out);
  if (status != CALL_OK) {
    return status;
  }
  if (!valid_child(directory, *out)) {
    discard_idle_inode(*out, false);
    *out = NULL;
    return corrupt(context);
  }
  npfs_store_retain(*out);
  return CALL_OK;
}

enum call_status npfs_store_enumerate(struct npfs_store_context *context, struct npfs_store_inode *directory,
  uint64_t generation, uint64_t position, struct npfs_dirent *entry,
  uint64_t *next_position, uint64_t *current_generation)
{
  require_owner();
  memset(entry, 0, sizeof(*entry));
  *current_generation = directory->generation;
  *next_position = position;
  if (directory->record.kind != NPFS_INODE_DIRECTORY) {
    return CALL_WRONG_TYPE;
  }
  if (generation && generation != directory->generation) {
    return CALL_OK;
  }
  if ((!generation && position) || position > directory->record.size) {
    return CALL_BAD_REQUEST;
  }
  struct directory_walk walk = {0};
  if (position < directory->record.size) {
    uint64_t boundary = position - position % NPFS_BLOCK_SIZE;
    while (boundary < position) {
      struct npfs_dirent previous;
      enum call_status status = directory_record(context, directory, &walk, boundary, &previous);
      if (status != CALL_OK) {
        return status;
      }
      boundary += previous.record_length;
    }
    if (boundary != position) {
      return CALL_BAD_REQUEST;
    }
  }
  while (position < directory->record.size) {
    enum call_status status = directory_record(context, directory, &walk, position, entry);
    if (status != CALL_OK) {
      return status;
    }
    position += entry->record_length;
    if (entry->inode) {
      struct npfs_store_inode *target;
      status = inode_get(context, directory->volume, entry->inode, &target);
      if (status != CALL_OK) {
        return status;
      }
      if (!valid_child(directory, target)) {
        discard_idle_inode(target, false);
        return corrupt(context);
      }
      discard_idle_inode(target, false);
      *next_position = position;
      return CALL_OK;
    }
  }
  memset(entry, 0, sizeof(*entry));
  *next_position = position;
  return CALL_OK;
}

static struct store_cache_entry *cache_find(struct npfs_store_inode *inode, uint64_t logical)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS; chunk++) {
    if (!pool->cache[chunk]) {
      continue;
    }
    for (unsigned i = 0; i < STORE_CHUNK_ENTRIES; i++) {
      struct store_cache_entry *entry = &pool->cache[chunk]->entries[i];
      if (entry->inode == inode && entry->logical == logical) {
        return entry;
      }
    }
  }
  return NULL;
}

static enum call_status cache_reserve(struct npfs_store_context *context,
  struct npfs_store_inode *inode, struct store_cache_entry **out)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  struct store_cache_entry *slot = NULL;
  bool owner_busy = false;
  for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS && !slot; chunk++) {
    if (!pool->cache[chunk]) {
      if (owner_busy) {
        continue;
      }
      uintptr_t address;
      enum call_status status = store_vm_allocate(sizeof(struct store_cache_chunk), &address);
      if (status != CALL_OK) {
        owner_busy |= status == CALL_BUSY;
        continue;
      }
      pool->cache[chunk] = (void *)address;
    }
    for (unsigned i = 0; i < STORE_CHUNK_ENTRIES; i++) {
      if (!pool->cache[chunk]->entries[i].inode) {
        slot = &pool->cache[chunk]->entries[i];
        break;
      }
    }
  }
  if (!slot) {
    for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS && !slot; chunk++) {
      if (!pool->cache[chunk]) {
        continue;
      }
      for (unsigned i = 0; i < STORE_CHUNK_ENTRIES; i++) {
        if (!pool->cache[chunk]->entries[i].dirty) {
          slot = &pool->cache[chunk]->entries[i];
          break;
        }
      }
    }
  }
  if (!slot) {
    enum call_status status = flush_files(context, pool);
    if (status != CALL_OK) {
      return status;
    }
    for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS && !slot; chunk++) {
      if (!pool->cache[chunk]) {
        continue;
      }
      for (unsigned i = 0; i < STORE_CHUNK_ENTRIES; i++) {
        if (!pool->cache[chunk]->entries[i].dirty) {
          slot = &pool->cache[chunk]->entries[i];
          break;
        }
      }
    }
  }
  if (!slot) {
    return owner_busy ? CALL_BUSY : CALL_NO_MEMORY;
  }
  struct npfs_store_inode *previous_owner = slot->inode;
  slot->inode = NULL;
  slot->dirty = false;
  if (previous_owner && previous_owner != inode) {
    discard_idle_inode(previous_owner, false);
  }
  *out = slot;
  return CALL_OK;
}

static enum call_status cache_fill(struct npfs_store_context *context,
  struct npfs_store_inode *inode, uint64_t logical, struct store_cache_entry *slot)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  uint64_t physical;
  enum call_status status = map_read(context, pool, inode->record.pointers, logical, &physical);
  if (status != CALL_OK) {
    return status;
  }
  if (physical) {
    status = read_block(context, pool, physical, slot->bytes);
  }
  else {
    memset(slot->bytes, 0, NPFS_BLOCK_SIZE);
  }
  if (status != CALL_OK) {
    return status;
  }
  slot->inode = inode;
  slot->logical = logical;
  slot->dirty = false;
  return CALL_OK;
}

static enum call_status cache_get(struct npfs_store_context *context, struct npfs_store_inode *inode,
  uint64_t logical, struct store_cache_entry **out)
{
  *out = cache_find(inode, logical);
  if (*out) {
    return CALL_OK;
  }
  struct store_cache_entry *slot;
  enum call_status status = cache_reserve(context, inode, &slot);
  if (status == CALL_OK) {
    status = cache_fill(context, inode, logical, slot);
  }
  if (status == CALL_OK) {
    *out = slot;
  }
  return status;
}

static enum call_status cache_read_run(struct npfs_store_context *context,
  struct npfs_store_inode *inode, uint64_t logical, uint32_t *count, void *bytes)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  uint64_t physical;
  enum call_status status = map_read(context, pool, inode->record.pointers, logical, &physical);
  if (status != CALL_OK) {
    return status;
  }
  if (!physical) {
    *count = 1;
    memset(bytes, 0, NPFS_BLOCK_SIZE);
    return CALL_OK;
  }
  if (image_find(pool, physical)) {
    *count = 1;
    return read_block(context, pool, physical, bytes);
  }
  uint32_t run = 1;
  while (run < *count) {
    uint64_t next;
    if (cache_find(inode, logical + run) ||
      !map_read_resident(pool, inode->record.pointers, logical + run, &next) ||
      next < physical || next - physical != run || image_find(pool, next)) {
      break;
    }
    run++;
  }
  *count = run;
  return transport(context, pool, BLOCK_READ, physical, run, bytes);
}

enum call_status npfs_store_read(struct npfs_store_context *context, struct npfs_store_inode *inode,
  uint64_t offset, void *bytes, size_t capacity, size_t *read)
{
  require_owner();
  *read = 0;
  if (inode->record.kind != NPFS_INODE_FILE) {
    return CALL_WRONG_TYPE;
  }
  if (offset >= inode->record.size) {
    return CALL_OK;
  }
  uint64_t remaining = inode->record.size - offset;
  size_t length = remaining < capacity ? (size_t)remaining : capacity;
  uint8_t *destination = bytes;
  struct npfs_store_pool *pool = inode->volume->pool;
  uint32_t run_limit = pool->device.max_transfer / NPFS_BLOCK_SIZE;
  if (run_limit > STORE_READ_RUN_BLOCKS) {
    run_limit = STORE_READ_RUN_BLOCKS;
  }
  size_t staging_bytes = (size_t)run_limit * NPFS_BLOCK_SIZE;
  uintptr_t staging = 0;
  bool staging_attempted = false;
  enum call_status status = CALL_OK;
  while (length) {
    enum call_status interrupted = completion_interruption(context);
    if (interrupted != CALL_OK) {
      status = *read && !context->bulk_read ? CALL_OK : interrupted;
      break;
    }
    unsigned within = (unsigned)(offset % NPFS_BLOCK_SIZE);
    size_t count = NPFS_BLOCK_SIZE - within;
    if (count > length) {
      count = length;
    }
    struct store_cache_entry *entry = NULL;
    uint64_t logical = offset / NPFS_BLOCK_SIZE;
    /* Partial pages and ordinary FILE calls keep the original single-page path. */
    if (!within && length >= 2 * NPFS_BLOCK_SIZE && run_limit >= 2) {
      entry = cache_find(inode, logical);
      if (!entry) {
        /* Mandatory cache storage must precede optional staging. */
        status = cache_reserve(context, inode, &entry);
        if (status != CALL_OK) {
          goto out;
        }
        if (!staging_attempted) {
          staging_attempted = true;
          uintptr_t address;
          if (store_vm_allocate(staging_bytes, &address) == CALL_OK) {
            staging = address;
          }
        }
        if (staging) {
          size_t available = length / NPFS_BLOCK_SIZE;
          uint32_t pages = available < run_limit ? (uint32_t)available : run_limit;
          status = cache_read_run(context, inode, logical, &pages, (void *)staging);
          if (status != CALL_OK) {
            goto out;
          }
          for (uint32_t page = 0; page < pages; page++) {
            interrupted = completion_interruption(context);
            if (interrupted != CALL_OK) {
              status = *read && !context->bulk_read ? CALL_OK : interrupted;
              goto out;
            }
            if (page) {
              status = cache_reserve(context, inode, &entry);
              if (status != CALL_OK) {
                goto out;
              }
            }
            memcpy(entry->bytes, (const uint8_t *)staging + (size_t)page * NPFS_BLOCK_SIZE,
              NPFS_BLOCK_SIZE);
            entry->inode = inode;
            entry->logical = logical + page;
            entry->dirty = false;
            /* Consume before the next reservation can evict this clean slot. */
            memcpy(destination, entry->bytes, NPFS_BLOCK_SIZE);
            destination += NPFS_BLOCK_SIZE;
            offset += NPFS_BLOCK_SIZE;
            length -= NPFS_BLOCK_SIZE;
            *read += NPFS_BLOCK_SIZE;
            if (context->bulk_read) {
              kernel_task_yield_if_runnable();
            }
          }
          continue;
        }
        status = cache_fill(context, inode, logical, entry);
        if (status != CALL_OK) {
          goto out;
        }
      }
    }
    if (!entry) {
      status = cache_get(context, inode, logical, &entry);
      if (status != CALL_OK) {
        break;
      }
    }
    memcpy(destination, entry->bytes + within, count);
    destination += count;
    offset += count;
    length -= count;
    *read += count;
    if (context->bulk_read) {
      kernel_task_yield_if_runnable();
    }
  }
out:
  store_vm_free(staging, staging_bytes);
  return status;
}

static struct store_cache_entry *next_dirty(struct npfs_store_inode *inode)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  struct store_cache_entry *result = NULL;
  for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS; chunk++) {
    if (!pool->cache[chunk]) {
      continue;
    }
    for (unsigned i = 0; i < STORE_CHUNK_ENTRIES; i++) {
      struct store_cache_entry *entry = &pool->cache[chunk]->entries[i];
      if (entry->inode == inode && entry->dirty && (!result || entry->logical < result->logical)) {
        result = entry;
      }
    }
  }
  return result;
}

static enum call_status flush_inode(struct npfs_store_context *context, struct npfs_store_inode *inode)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  if (!pool->write_run && next_dirty(inode)) {
    /* Batching is optional; allocation pressure keeps the single-block path. */
    (void)store_vm_allocate(STORE_WRITE_RUN_BLOCKS * NPFS_BLOCK_SIZE, &pool->write_run);
  }
  while (inode->size_dirty || next_dirty(inode)) {
    status = operation_begin(context, pool, STORE_WRITEBACK_IMAGES, STORE_MAPPING_BLOCKS);
    if (status != CALL_OK) {
      return status;
    }
    struct npfs_inode previous = inode->record;
    struct store_cache_entry *written[STORE_IMAGES_MAX];
    uint64_t physical_blocks[STORE_IMAGES_MAX];
    unsigned written_count = 0;
    struct store_cache_entry *entry = next_dirty(inode);
    while (entry && pool->image_capacity - pool->image_count >= STORE_WRITEBACK_IMAGES &&
      STORE_OPERATION_IMAGES - pool->undo_count >= STORE_WRITEBACK_IMAGES &&
      !allocation_short(pool, STORE_MAPPING_BLOCKS) && written_count < STORE_IMAGES_MAX) {
      uint64_t physical;
      status = map_ensure(context, pool, inode->record.pointers, entry->logical, 0, &physical);
      if (status != CALL_OK) {
        break;
      }
      entry->dirty = false;
      physical_blocks[written_count] = physical;
      written[written_count++] = entry;
      entry = next_dirty(inode);
    }
    for (unsigned i = 0; status == CALL_OK && i < written_count;) {
      unsigned count = 1;
      if (pool->write_run) {
        while (count < written_count - i && count < STORE_WRITE_RUN_BLOCKS &&
          physical_blocks[i + count] == physical_blocks[i] + count) {
          count++;
        }
      }
      void *bytes = written[i]->bytes;
      if (count > 1) {
        bytes = (void *)pool->write_run;
        for (unsigned j = 0; j < count; j++) {
          memcpy((uint8_t *)bytes + (size_t)j * NPFS_BLOCK_SIZE,
            written[i + j]->bytes, NPFS_BLOCK_SIZE);
        }
      }
      status = transport(context, pool, BLOCK_WRITE, physical_blocks[i], count, bytes);
      if (status != CALL_OK) {
        latch_failure(pool, status);
      }
      i += count;
    }
    if (status == CALL_OK) {
      struct npfs_inode durable = inode->record;
      uint64_t prefix = entry ? entry->logical * NPFS_BLOCK_SIZE : inode->record.size;
      if (prefix < inode->durable_size) {
        prefix = inode->durable_size;
      }
      durable.size = prefix;
      if (durable.cleanup & NPFS_CLEANUP_SHRINK) {
        durable.shrink_target = prefix;
      }
      /* The data above is on the device; the staged mapping and size commit
       * with the running transaction, after the commit's device flush. */
      status = stage_inode_record(context, inode, &durable);
      if (status == CALL_OK) {
        operation_end(pool);
        inode->durable = durable;
        inode->durable_size = durable.size;
        inode->size_dirty = entry != NULL;
      }
    }
    if (status != CALL_OK) {
      for (unsigned i = 0; i < written_count; i++) {
        written[i]->dirty = true;
      }
      inode->record = previous;
      operation_abort(pool);
      if (pool->writeback_error == CALL_OK) {
        pool->writeback_error = status;
      }
      if (inode->writeback_error == CALL_OK) {
        inode->writeback_error = status;
      }
      return status;
    }
    kernel_task_yield_if_runnable();
  }
  return CALL_OK;
}

static enum call_status flush_files(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  if (pool->failed) {
    return pool->writeback_error;
  }
  for (struct npfs_store_volume *volume = pool->volumes; volume; volume = volume->next) {
    for (struct npfs_store_inode *inode = volume->inodes; inode; inode = inode->next) {
      if (inode->record.kind != NPFS_INODE_FILE ||
        (!inode->references && inode->record.cleanup & NPFS_CLEANUP_DETACHED)) {
        continue;
      }
      if (inode->size_dirty || next_dirty(inode)) {
        enum call_status status = flush_inode(context, inode);
        if (status != CALL_OK) {
          return status;
        }
      }
    }
  }
  return CALL_OK;
}

static enum call_status finish_shrink(struct npfs_store_context *context, struct npfs_store_inode *inode)
{
  while (inode->record.cleanup & NPFS_CLEANUP_SHRINK) {
    enum call_status status = cleanup_one(context, inode->volume);
    if (status != CALL_OK) {
      return status;
    }
    kernel_task_yield_if_runnable();
  }
  return CALL_OK;
}

enum call_status npfs_store_write(struct npfs_store_context *context, struct npfs_store_inode *inode,
  uint64_t offset, const void *bytes, size_t length, size_t *written)
{
  require_owner();
  *written = 0;
  enum call_status status = writable(inode->volume->pool);
  if (status != CALL_OK) {
    return status;
  }
  if (inode->record.kind != NPFS_INODE_FILE) {
    return CALL_WRONG_TYPE;
  }
  if (offset > NPFS_FILE_SIZE_MAX || length > NPFS_FILE_SIZE_MAX - offset) {
    return CALL_FILE_TOO_LARGE;
  }
  if (!length) {
    return CALL_OK;
  }
  status = completion_interruption(context);
  if (status != CALL_OK) {
    return status;
  }
  status = finish_shrink(context, inode);
  if (status != CALL_OK) {
    return status;
  }
  if (offset > inode->record.size) {
    status = zero_growth_tail(context, inode);
    if (status != CALL_OK) {
      return status;
    }
  }
  const uint8_t *source = bytes;
  while (length) {
    status = completion_interruption(context);
    if (status != CALL_OK) {
      return *written ? CALL_OK : status;
    }
    unsigned within = (unsigned)(offset % NPFS_BLOCK_SIZE);
    size_t count = NPFS_BLOCK_SIZE - within;
    if (count > length) {
      count = length;
    }
    struct store_cache_entry *entry;
    status = cache_get(context, inode, offset / NPFS_BLOCK_SIZE, &entry);
    if (status != CALL_OK) {
      return *written ? CALL_OK : status;
    }
    uint64_t old_size = inode->record.size;
    uint64_t base = offset - within;
    if (old_size < base + NPFS_BLOCK_SIZE) {
      size_t start = old_size > base ? (size_t)(old_size - base) : 0;
      memset(entry->bytes + start, 0, NPFS_BLOCK_SIZE - start);
    }
    memcpy(entry->bytes + within, source, count);
    entry->dirty = true;
    if (offset + count > inode->record.size) {
      inode->record.size = offset + count;
    }
    inode->size_dirty = true;
    touch_inode(context, &inode->record, false);
    *written += count;
    source += count;
    offset += count;
    length -= count;
  }
  return CALL_OK;
}

enum call_status npfs_store_sync(struct npfs_store_context *context, struct npfs_store_pool *pool)
{
  require_owner();
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  status = flush_files(context, pool);
  if (status == CALL_OK) {
    status = commit_running(context, pool);
  }
  if (!pool->failed) {
    if (status == CALL_OK) {
      status = pool->writeback_error;
    }
    pool->writeback_error = CALL_OK;
  }
  return status;
}

enum call_status npfs_store_sync_file(struct npfs_store_context *context, struct npfs_store_inode *inode)
{
  require_owner();
  struct npfs_store_pool *pool = inode->volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  status = flush_inode(context, inode);
  if (status == CALL_OK) {
    status = commit_running(context, pool);
  }
  if (!pool->failed) {
    if (status == CALL_OK) {
      status = inode->writeback_error;
    }
    inode->writeback_error = CALL_OK;
  }
  return status;
}

/* Namespace operations join the running transaction and return without
 * committing it; sync, background writeback, pressure, power-off and journal
 * capacity commit it. */
static enum call_status begin_namespace(struct npfs_store_context *context, struct npfs_store_pool *pool,
  bool allocates)
{
  if (next_generation > UINT64_MAX - 4) {
    return CALL_LIMIT;
  }
  return operation_begin(context, pool, STORE_OPERATION_IMAGES,
    allocates ? STORE_OPERATION_BLOCKS : 0);
}

static enum call_status directory_edit(struct npfs_store_context *context, struct npfs_store_inode *directory,
  uint64_t position, uint8_t **bytes)
{
  uint64_t physical;
  enum call_status status = map_read(context, directory->volume->pool, directory->record.pointers,
    position / NPFS_BLOCK_SIZE, &physical);
  if (status != CALL_OK) {
    return status;
  }
  if (!physical) {
    return corrupt(context);
  }
  return edit_block(context, directory->volume->pool, physical, NPFS_METADATA_DIRECTORY, false, bytes);
}

static enum call_status directory_clear(struct npfs_store_context *context, struct npfs_store_inode *directory,
  uint64_t position, uint16_t record_length)
{
  uint8_t *bytes;
  enum call_status status = directory_edit(context, directory, position, &bytes);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_dirent free_entry = { .record_length = record_length };
  return format_failure(context, npfs_dirent_encode(&directory->volume->pool->header,
    &free_entry, bytes + position % NPFS_BLOCK_SIZE));
}

static enum call_status directory_insert(struct npfs_store_context *context, struct npfs_store_inode *directory,
  const char *name, size_t length, uint64_t number)
{
  if (directory->record.cleanup & NPFS_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  uint16_t needed = (uint16_t)((NPFS_DIRENT_HEADER_SIZE + length + 7) & ~7u);
  uint64_t position = 0;
  struct npfs_dirent free_entry = {0};
  struct directory_walk walk = {0};
  bool available = false;
  while (position < directory->record.size) {
    enum call_status status = directory_record(context, directory, &walk, position, &free_entry);
    if (status != CALL_OK) {
      return status;
    }
    if (!free_entry.inode && free_entry.record_length >= needed) {
      available = true;
      break;
    }
    position += free_entry.record_length;
  }
  struct npfs_store_pool *pool = directory->volume->pool;
  uint8_t *bytes;
  enum call_status status;
  if (!available) {
    if (directory->record.size > NPFS_FILE_SIZE_MAX - NPFS_BLOCK_SIZE) {
      return CALL_FILE_TOO_LARGE;
    }
    uint64_t physical;
    position = directory->record.size;
    status = map_ensure(context, pool, directory->record.pointers,
      position / NPFS_BLOCK_SIZE, NPFS_METADATA_DIRECTORY, &physical);
    if (status != CALL_OK) {
      return status;
    }
    status = edit_block(context, pool, physical, NPFS_METADATA_DIRECTORY, false, &bytes);
    if (status != CALL_OK) {
      return status;
    }
    free_entry.record_length = NPFS_BLOCK_SIZE;
    directory->record.size += NPFS_BLOCK_SIZE;
  } else {
    status = directory_edit(context, directory, position, &bytes);
    if (status != CALL_OK) {
      return status;
    }
  }
  uint16_t remaining = free_entry.record_length - needed;
  struct npfs_dirent entry = {
    .inode = number,
    .record_length = remaining >= NPFS_DIRENT_HEADER_SIZE ? needed : free_entry.record_length,
    .name_length = (uint16_t)length,
  };
  memcpy(entry.name, name, length);
  unsigned offset = (unsigned)(position % NPFS_BLOCK_SIZE);
  status = format_failure(context, npfs_dirent_encode(&pool->header, &entry, bytes + offset));
  if (status == CALL_OK && remaining >= NPFS_DIRENT_HEADER_SIZE) {
    struct npfs_dirent tail = { .record_length = remaining };
    status = format_failure(context, npfs_dirent_encode(&pool->header, &tail, bytes + offset + needed));
  }
  return status;
}

static enum call_status directory_empty(struct npfs_store_context *context, struct npfs_store_inode *directory)
{
  struct directory_walk walk = {0};
  for (uint64_t position = 0; position < directory->record.size;) {
    struct npfs_dirent entry;
    enum call_status status = directory_record(context, directory, &walk, position, &entry);
    if (status != CALL_OK) {
      return status;
    }
    if (entry.inode) {
      return CALL_NOT_EMPTY;
    }
    position += entry.record_length;
  }
  return CALL_OK;
}

static enum call_status cleanup_link(struct npfs_store_context *context, struct npfs_store_inode *inode, uint32_t flag)
{
  struct npfs_store_volume *volume = inode->volume;
  if (!inode->record.cleanup) {
    inode->record.cleanup_next = volume->record.cleanup_head;
    volume->record.cleanup_head = inode->number;
  }
  inode->record.cleanup |= flag;
  if (flag & NPFS_CLEANUP_DETACHED) {
    inode->record.parent = 0;
    inode->record.link_count = 0;
  }
  return stage_volume(context, volume);
}

static enum call_status remove_name(struct npfs_store_context *context,
  struct npfs_store_inode *inode, struct npfs_inode *staged)
{
  enum call_status status = CALL_OK;
  if (inode->record.kind == NPFS_INODE_FILE) {
    if (!inode->record.link_count) {
      return corrupt(context);
    }
    inode->record.link_count--;
  }
  if (inode->record.kind == NPFS_INODE_DIRECTORY || !inode->record.link_count) {
    status = cleanup_link(context, inode, NPFS_CLEANUP_DETACHED);
  }
  if (status != CALL_OK) {
    return status;
  }
  /* Namespace edits must not publish cached growth or unwritten mappings. */
  *staged = inode->durable;
  staged->link_count = inode->record.link_count;
  staged->cleanup = inode->record.cleanup;
  staged->cleanup_next = inode->record.cleanup_next;
  staged->parent = inode->record.parent;
  return stage_inode_record(context, inode, staged);
}

enum call_status npfs_store_create(struct npfs_store_context *context, struct npfs_store_inode *directory,
  const char *name, size_t length, uint64_t kind, struct npfs_store_inode **out)
{
  require_owner();
  *out = NULL;
  if (directory->record.kind != NPFS_INODE_DIRECTORY) {
    return CALL_WRONG_TYPE;
  }
  if (!npfs_name_valid((const uint8_t *)name, length) ||
    (kind != DIRECTORY_KIND_FILE && kind != DIRECTORY_KIND_DIRECTORY)) {
    return CALL_BAD_REQUEST;
  }
  if (directory->record.cleanup & NPFS_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  struct npfs_store_volume *volume = directory->volume;
  struct npfs_store_pool *pool = volume->pool;
  enum call_status status = completion_interruption(context);
  if (status != CALL_OK) {
    return status;
  }
  status = begin_namespace(context, pool, true);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_dirent existing;
  uint64_t position;
  status = directory_find(context, directory, name, length, &existing, &position);
  if (status != CALL_NOT_FOUND) {
    operation_end(pool);
    return status == CALL_OK ? CALL_ALREADY_EXISTS : status;
  }
  status = completion_interruption(context);
  if (status != CALL_OK) {
    operation_end(pool);
    return status;
  }
  uint64_t identity;
  if (!allocate_identity(&identity)) {
    operation_end(pool);
    return CALL_LIMIT;
  }
  struct npfs_inode parent_previous = directory->record;
  struct npfs_volume volume_previous = volume->record;
  struct store_free_slot *prepared_slots = NULL;
  struct store_free_slot *selected = volume->free_slots;
  uint64_t number;
  if (selected) {
    number = selected->number;
  } else {
    uint64_t old_bytes = volume->record.inode_bytes;
    uint64_t new_bytes = (old_bytes + NPFS_BLOCK_SIZE) & ~(uint64_t)(NPFS_BLOCK_SIZE - 1);
    if (new_bytes > NPFS_FILE_SIZE_MAX) {
      operation_end(pool);
      return CALL_FILE_TOO_LARGE;
    }
    number = old_bytes / NPFS_INODE_SIZE;
    for (uint64_t slot = number + 1; slot < new_bytes / NPFS_INODE_SIZE; slot++) {
      struct store_free_slot *free_slot = store_allocate(sizeof(*free_slot));
      if (!free_slot) {
        status = CALL_NO_MEMORY;
        goto fail;
      }
      free_slot->heap_owned = true;
      free_slot->number = slot;
      free_slot->next = prepared_slots;
      prepared_slots = free_slot;
    }
    uint64_t physical;
    status = map_ensure(context, pool, volume->record.pointers,
      old_bytes / NPFS_BLOCK_SIZE, NPFS_METADATA_INODES, &physical);
    if (status != CALL_OK) {
      goto fail;
    }
    uint8_t *bytes;
    status = edit_block(context, pool, physical, NPFS_METADATA_INODES, false, &bytes);
    if (status != CALL_OK) {
      goto fail;
    }
    memset(bytes + old_bytes % NPFS_BLOCK_SIZE, 0, NPFS_BLOCK_SIZE - old_bytes % NPFS_BLOCK_SIZE);
    volume->record.inode_bytes = new_bytes;
    status = stage_volume(context, volume);
    if (status != CALL_OK) {
      goto fail;
    }
  }
  struct npfs_store_inode *child;
  status = inode_get(context, volume, number, &child);
  if (status != CALL_OK) {
    goto fail;
  }
  if (child->record.kind != NPFS_INODE_FREE || child->references) {
    status = corrupt(context);
    goto fail;
  }
  child->record = (struct npfs_inode){
    .kind = kind == DIRECTORY_KIND_DIRECTORY ? NPFS_INODE_DIRECTORY : NPFS_INODE_FILE,
    .mapping = NPFS_MAPPING_POINTERS,
    .parent = kind == DIRECTORY_KIND_DIRECTORY ? directory->number : 0,
    .link_count = kind == DIRECTORY_KIND_FILE ? 1 : 0,
  };
  touch_inode(context, &child->record, true);
  status = directory_insert(context, directory, name, length, number);
  touch_inode(context, &directory->record, false);
  if (status == CALL_OK) {
    status = stage_inode(context, child);
  }
  if (status == CALL_OK) {
    status = stage_inode(context, directory);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      memset(&child->record, 0, sizeof(child->record));
    }
    goto fail;
  }
  operation_end(pool);
  if (selected) {
    volume->free_slots = selected->next;
    if (selected->heap_owned) {
      store_free(selected);
    }
  } else {
    volume->free_slots = prepared_slots;
    prepared_slots = NULL;
  }
  child->durable = child->record;
  child->durable_size = 0;
  child->identity = identity;
  child->generation = ++next_generation;
  directory->durable = directory->record;
  directory->durable_size = directory->record.size;
  directory->generation = ++next_generation;
  npfs_store_retain(child);
  *out = child;
  return CALL_OK;
  fail:
  while (prepared_slots) {
    struct store_free_slot *next = prepared_slots->next;
    store_free(prepared_slots);
    prepared_slots = next;
  }
  if (!pool->failed) {
    directory->record = parent_previous;
    volume->record = volume_previous;
  }
  operation_abort(pool);
  return status;
}

enum call_status npfs_store_create_volume(struct npfs_store_context *context,
  struct npfs_store_pool *pool, const char *name, size_t length, const uint8_t id[NPFS_ID_SIZE])
{
  require_owner();
  if (!npfs_name_valid((const uint8_t *)name, length) || !npfs_id_valid(id)) {
    return CALL_BAD_REQUEST;
  }
  enum call_status status = begin_namespace(context, pool, true);
  if (status != CALL_OK) {
    return status;
  }
  unsigned slot = NPFS_VOLUME_COUNT;
  for (unsigned i = 0; i < NPFS_VOLUME_COUNT; i++) {
    const struct npfs_volume *record = &pool->catalog[i];
    if (record->state != NPFS_VOLUME_LIVE) {
      if (slot == NPFS_VOLUME_COUNT) {
        slot = i;
      }
      continue;
    }
    if (record->name_length == length && !memcmp(record->name, name, length)) {
      operation_end(pool);
      return CALL_ALREADY_EXISTS;
    }
    if (!memcmp(record->id, id, NPFS_ID_SIZE)) {
      operation_end(pool);
      return CALL_BAD_REQUEST;
    }
  }
  if (slot == NPFS_VOLUME_COUNT) {
    operation_end(pool);
    return CALL_LIMIT;
  }

  /* One transaction: the inode file holding root inode 1, the empty root
   * directory, their bitmap bits and the live volume record. */
  uint64_t inode_block = 0, directory_block = 0;
  uint8_t *inodes, *directory, *records;
  status = allocate_block(context, pool, &inode_block);
  if (status == CALL_OK) {
    status = allocate_block(context, pool, &directory_block);
  }
  if (status == CALL_OK) {
    status = edit_block(context, pool, inode_block, NPFS_METADATA_INODES, true, &inodes);
  }
  if (status == CALL_OK) {
    status = edit_block(context, pool, directory_block, NPFS_METADATA_DIRECTORY, true, &directory);
  }
  if (status == CALL_OK) {
    status = edit_block(context, pool, pool->header.volume_start + slot / 8,
      NPFS_METADATA_VOLUMES, false, &records);
  }
  if (status != CALL_OK) {
    operation_abort(pool);
    return status;
  }
  struct npfs_inode root = {
    .kind = NPFS_INODE_DIRECTORY,
    .mapping = NPFS_MAPPING_POINTERS,
    .parent = 1,
    .size = NPFS_BLOCK_SIZE,
    .pointers = {directory_block},
  };
  touch_inode(context, &root, true);
  struct npfs_dirent unused = {.record_length = NPFS_BLOCK_SIZE};
  struct npfs_volume volume = {
    .state = NPFS_VOLUME_LIVE,
    .mapping = NPFS_MAPPING_POINTERS,
    .name_length = (uint16_t)length,
    .root_inode = 1,
    .inode_bytes = NPFS_BLOCK_SIZE,
    .pointers = {inode_block},
  };
  memcpy(volume.id, id, NPFS_ID_SIZE);
  memcpy(volume.name, name, length);
  enum npfs_status encoded = npfs_inode_encode(&pool->header, &root, inodes + NPFS_INODE_SIZE);
  if (encoded == NPFS_OK) {
    encoded = npfs_dirent_encode(&pool->header, &unused, directory);
  }
  if (encoded == NPFS_OK) {
    encoded = npfs_volume_encode(&pool->header, &volume,
      records + (slot % 8) * NPFS_VOLUME_SIZE);
  }
  if (encoded != NPFS_OK) {
    operation_abort(pool);
    return format_failure(context, encoded);
  }
  operation_end(pool);
  pool->catalog[slot] = volume;
  return CALL_OK;
}

enum call_status npfs_store_remove(struct npfs_store_context *context, struct npfs_store_inode *directory,
  const char *name, size_t length, uint64_t kind)
{
  require_owner();
  if (directory->record.cleanup & NPFS_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  struct npfs_store_pool *pool = directory->volume->pool;
  enum call_status status = completion_interruption(context);
  if (status != CALL_OK) {
    return status;
  }
  status = begin_namespace(context, pool, false);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_dirent entry;
  uint64_t position;
  status = directory_find(context, directory, name, length, &entry, &position);
  if (status != CALL_OK) {
    operation_end(pool);
    return status;
  }
  struct npfs_store_inode *target;
  status = inode_get(context, directory->volume, entry.inode, &target);
  if (status != CALL_OK) {
    operation_end(pool);
    return status;
  }
  if (!valid_child(directory, target)) {
    operation_end(pool);
    discard_idle_inode(target, false);
    return corrupt(context);
  }
  if (kind != DIRECTORY_KIND_ANY && kind != npfs_store_kind(target)) {
    operation_end(pool);
    discard_idle_inode(target, false);
    return CALL_WRONG_TYPE;
  }
  if (target->record.kind == NPFS_INODE_DIRECTORY) {
    status = directory_empty(context, target);
    if (status != CALL_OK) {
      operation_end(pool);
      discard_idle_inode(target, false);
      return status;
    }
  }
  status = completion_interruption(context);
  if (status != CALL_OK) {
    operation_end(pool);
    discard_idle_inode(target, false);
    return status;
  }
  struct npfs_inode parent_previous = directory->record;
  struct npfs_inode target_previous = target->record;
  struct npfs_volume volume_previous = directory->volume->record;
  status = directory_clear(context, directory, position, entry.record_length);
  struct npfs_inode staged;
  if (status == CALL_OK) {
    status = remove_name(context, target, &staged);
  }
  touch_inode(context, &directory->record, false);
  if (status == CALL_OK) {
    status = stage_inode(context, directory);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      directory->record = parent_previous;
      target->record = target_previous;
      directory->volume->record = volume_previous;
    }
    operation_abort(pool);
    discard_idle_inode(target, false);
    return status;
  }
  operation_end(pool);
  target->durable = staged;
  directory->durable = directory->record;
  directory->generation = ++next_generation;
  discard_idle_inode(target, false);
  return CALL_OK;
}

enum call_status npfs_store_link(struct npfs_store_context *context, struct npfs_store_inode *source,
  const char *source_name, size_t source_length, struct npfs_store_inode *destination,
  const char *destination_name, size_t destination_length)
{
  require_owner();
  if (source->volume != destination->volume) {
    return CALL_BAD_OPERATION;
  }
  if (source->record.kind != NPFS_INODE_DIRECTORY || destination->record.kind != NPFS_INODE_DIRECTORY) {
    return CALL_WRONG_TYPE;
  }
  if (!npfs_name_valid((const uint8_t *)source_name, source_length) ||
    !npfs_name_valid((const uint8_t *)destination_name, destination_length)) {
    return CALL_BAD_REQUEST;
  }
  if ((source->record.cleanup | destination->record.cleanup) & NPFS_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  struct npfs_store_pool *pool = source->volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_dirent entry;
  uint64_t position;
  status = directory_find(context, source, source_name, source_length, &entry, &position);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_store_inode *target;
  status = inode_get(context, source->volume, entry.inode, &target);
  if (status != CALL_OK) {
    return status;
  }
  if (!valid_child(source, target)) {
    status = corrupt(context);
    goto out;
  }
  if (target->record.kind != NPFS_INODE_FILE) {
    status = CALL_WRONG_TYPE;
    goto out;
  }
  if (target->record.link_count == UINT32_MAX) {
    status = CALL_LIMIT;
    goto out;
  }
  status = directory_find(context, destination, destination_name, destination_length, &entry, &position);
  if (status != CALL_NOT_FOUND) {
    status = status == CALL_OK ? CALL_ALREADY_EXISTS : status;
    goto out;
  }
  status = completion_interruption(context);
  if (status == CALL_OK) {
    status = begin_namespace(context, pool, true);
  }
  if (status != CALL_OK) {
    goto out;
  }
  status = enable_hard_links(context, pool);
  if (status != CALL_OK) {
    operation_end(pool);
    goto out;
  }
  struct npfs_inode parent_previous = destination->record;
  struct npfs_inode staged = target->durable;
  staged.link_count = target->record.link_count + 1;
  status = directory_insert(context, destination, destination_name, destination_length, target->number);
  if (status == CALL_OK) {
    status = stage_inode_record(context, target, &staged);
  }
  touch_inode(context, &destination->record, false);
  if (status == CALL_OK) {
    status = stage_inode(context, destination);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      destination->record = parent_previous;
    }
    operation_abort(pool);
    goto out;
  }
  operation_end(pool);
  target->record.link_count = staged.link_count;
  target->durable = staged;
  destination->durable = destination->record;
  destination->generation = ++next_generation;
out:
  discard_idle_inode(target, false);
  return status;
}

static enum call_status directory_parent(struct npfs_store_context *context,
  struct npfs_store_volume *volume, uint64_t number, uint64_t moved, uint64_t *parent)
{
  if (number == moved) {
    return CALL_BAD_REQUEST;
  }
  uint64_t count = volume->record.inode_bytes / NPFS_INODE_SIZE;
  if (!number || number >= count) {
    return corrupt(context);
  }
  struct npfs_inode record;
  struct npfs_store_inode *cached = volume->inodes;
  while (cached && cached->number != number) {
    cached = cached->next;
  }
  if (cached) {
    record = cached->record;
  } else {
    enum call_status status = inode_record_read(context, volume, number, &record);
    if (status != CALL_OK) {
      return status;
    }
  }
  if (record.kind != NPFS_INODE_DIRECTORY || (record.cleanup & NPFS_CLEANUP_DETACHED) ||
    !record.parent || record.parent >= count ||
    (number == 1 ? record.parent != 1 : record.parent == number)) {
    return corrupt(context);
  }
  *parent = record.parent;
  return CALL_OK;
}

static enum call_status check_directory_ancestry(struct npfs_store_context *context,
  struct npfs_store_inode *moved, struct npfs_store_inode *destination)
{
  uint64_t slow = destination->number, fast = slow;
  /* Copied records avoid allocating ancestor views or borrowing them across I/O.
   * The worker owns the entire walk/edit; two cursors detect corrupt cycles. */
  for (unsigned step = 0; step < STORE_ANCESTRY_STEPS; step++) {
    enum call_status status = directory_parent(context, moved->volume, slow, moved->number, &slow);
    if (status == CALL_OK) {
      status = directory_parent(context, moved->volume, fast, moved->number, &fast);
    }
    if (status == CALL_OK) {
      status = directory_parent(context, moved->volume, fast, moved->number, &fast);
    }
    if (status != CALL_OK) {
      return status;
    }
    if (fast == 1) {
      return directory_parent(context, moved->volume, fast, moved->number, &fast);
    }
    if (slow == fast) {
      return corrupt(context);
    }
  }
  return CALL_LIMIT;
}

enum call_status npfs_store_rename(struct npfs_store_context *context, struct npfs_store_inode *source,
  const char *source_name, size_t source_length,
  struct npfs_store_inode *destination, const char *destination_name,
  size_t destination_length, bool replace, uint64_t kind)
{
  require_owner();
  if (kind != DIRECTORY_KIND_ANY && kind != DIRECTORY_KIND_FILE && kind != DIRECTORY_KIND_DIRECTORY) {
    return CALL_BAD_REQUEST;
  }
  if (source->volume != destination->volume) {
    return CALL_BAD_OPERATION;
  }
  if (source->record.cleanup & NPFS_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  if (!npfs_name_valid((const uint8_t *)destination_name, destination_length) ||
    destination->record.kind != NPFS_INODE_DIRECTORY) {
    return CALL_BAD_REQUEST;
  }
  if (destination->record.cleanup & NPFS_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  struct npfs_store_pool *pool = source->volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_dirent source_entry, destination_entry;
  uint64_t source_position, destination_position = 0;
  status = directory_find(context, source, source_name, source_length, &source_entry, &source_position);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_store_inode *moved;
  status = inode_get(context, source->volume, source_entry.inode, &moved);
  if (status != CALL_OK) {
    return status;
  }
  if (!valid_child(source, moved) || moved == source) {
    discard_idle_inode(moved, false);
    return corrupt(context);
  }
  if (kind != DIRECTORY_KIND_ANY && kind != npfs_store_kind(moved)) {
    discard_idle_inode(moved, false);
    return CALL_WRONG_TYPE;
  }
  if (source == destination && source_length == destination_length &&
    !memcmp(source_name, destination_name, source_length)) {
    discard_idle_inode(moved, false);
    return CALL_OK;
  }
  bool directory = moved->record.kind == NPFS_INODE_DIRECTORY;
  if (directory) {
    status = check_directory_ancestry(context, moved, destination);
    if (status != CALL_OK) {
      discard_idle_inode(moved, false);
      return status;
    }
  }
  status = directory_find(context, destination, destination_name, destination_length,
    &destination_entry, &destination_position);
  struct npfs_store_inode *victim = NULL;
  if (status == CALL_OK) {
    if (destination_entry.inode == moved->number) {
      status = !directory && moved->record.link_count >= 2 ? CALL_OK : corrupt(context);
      discard_idle_inode(moved, false);
      return status;
    }
    if (!replace) {
      discard_idle_inode(moved, false);
      return CALL_ALREADY_EXISTS;
    }
    status = inode_get(context, destination->volume, destination_entry.inode, &victim);
    if (status != CALL_OK) {
      discard_idle_inode(moved, false);
      return status;
    }
    if (!valid_child(destination, victim)) {
      if (victim != moved) {
        discard_idle_inode(victim, false);
      }
      discard_idle_inode(moved, false);
      return corrupt(context);
    }
    if (victim->record.kind != moved->record.kind || victim == moved) {
      if (victim != moved) {
        discard_idle_inode(victim, false);
      }
      discard_idle_inode(moved, false);
      return CALL_WRONG_TYPE;
    }
    if (directory) {
      status = directory_empty(context, victim);
      if (status != CALL_OK) {
        discard_idle_inode(victim, false);
        discard_idle_inode(moved, false);
        return status;
      }
    }
  } else if (status != CALL_NOT_FOUND) {
    discard_idle_inode(moved, false);
    return status;
  }
  /* File replacement writes the new contents and stages their mapping before
   * the name moves; commits are ordered, so the name never outlives them. */
  status = victim && !directory ? flush_inode(context, moved) : CALL_OK;
  if (status == CALL_OK) {
    status = begin_namespace(context, pool, true);
  }
  if (status != CALL_OK) {
    discard_idle_inode(moved, false);
    if (victim) {
      discard_idle_inode(victim, false);
    }
    return status;
  }
  struct npfs_inode source_previous = source->record;
  struct npfs_inode destination_previous = destination->record;
  struct npfs_inode moved_previous = moved->record;
  struct npfs_inode victim_previous = victim ? victim->record : (struct npfs_inode){0};
  struct npfs_inode victim_staged = {0};
  struct npfs_volume volume_previous = source->volume->record;
  if (victim) {
    status = directory_clear(context, destination, destination_position, destination_entry.record_length);
    if (status == CALL_OK) {
      status = remove_name(context, victim, &victim_staged);
    }
  } else {
    status = CALL_OK;
  }
  if (status == CALL_OK) {
    status = directory_insert(context, destination, destination_name, destination_length, moved->number);
  }
  if (status == CALL_OK) {
    status = directory_clear(context, source, source_position, source_entry.record_length);
  }
  touch_inode(context, &source->record, false);
  touch_inode(context, &destination->record, false);
  if (status == CALL_OK) {
    status = stage_inode(context, source);
  }
  if (status == CALL_OK && destination != source) {
    status = stage_inode(context, destination);
  }
  if (status == CALL_OK && directory && source != destination) {
    moved->record.parent = destination->number;
    status = stage_inode(context, moved);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      moved->record = moved_previous;
      source->record = source_previous;
      destination->record = destination_previous;
      source->volume->record = volume_previous;
      if (victim) {
        victim->record = victim_previous;
      }
    }
    operation_abort(pool);
    discard_idle_inode(moved, false);
    if (victim) {
      discard_idle_inode(victim, false);
    }
    return status;
  }
  operation_end(pool);
  if (directory && source != destination) {
    moved->durable = moved->record;
  }
  source->durable = source->record;
  destination->durable = destination->record;
  source->generation = ++next_generation;
  if (destination != source) {
    destination->generation = ++next_generation;
  }
  if (victim) {
    victim->durable = victim_staged;
    discard_idle_inode(victim, false);
  }
  discard_idle_inode(moved, false);
  return CALL_OK;
}

struct cleanup_path {
  uint64_t target;
  unsigned root_slot, depth;
  uint64_t parents[3];
  unsigned indices[3];
};

static enum call_status highest_mapping(struct npfs_store_context *context, struct npfs_store_inode *inode,
  uint64_t limit, struct cleanup_path *path)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  const uint64_t *pointers = inode->durable.pointers;
  memset(path, 0, sizeof(*path));
  struct search_frame {
    uint64_t block, base, span;
    unsigned level, cursor, child_index;
  } frames[3];
  uint64_t bases[3] = {NPFS_DIRECT_COUNT, NPFS_DIRECT_COUNT + NPFS_INDIRECT_COUNT,
    NPFS_DIRECT_COUNT + NPFS_INDIRECT_COUNT + (uint64_t)NPFS_INDIRECT_COUNT * NPFS_INDIRECT_COUNT};
  uint64_t spans[3] = {1, NPFS_INDIRECT_COUNT, (uint64_t)NPFS_INDIRECT_COUNT * NPFS_INDIRECT_COUNT};
  for (unsigned tier = 3; tier; tier--) {
    unsigned root_slot = NPFS_DIRECT_COUNT + tier - 1;
    if (!pointers[root_slot]) {
      continue;
    }
    frames[0] = (struct search_frame){pointers[root_slot], bases[tier - 1], spans[tier - 1], tier, NPFS_INDIRECT_COUNT, 0};
    unsigned depth = 1;
    while (depth) {
      struct search_frame *frame = &frames[depth - 1];
      enum call_status status = allocated_block(context, pool, frame->block);
      if (status != CALL_OK) {
        return status;
      }
      status = read_metadata_block(context, pool, frame->block, pool->io[0]);
      if (status != CALL_OK) {
        return status;
      }
      bool empty = true;
      for (unsigned i = 0; i < NPFS_INDIRECT_COUNT; i++) {
        if (npfs_get_u64(pool->io[0] + i * 8)) {
          empty = false;
          break;
        }
      }
      if (empty) {
        path->target = frame->block;
        path->root_slot = root_slot;
        path->depth = depth - 1;
        for (unsigned i = 0; i < path->depth; i++) {
          path->parents[i] = frames[i].block;
          path->indices[i] = frames[i].child_index;
        }
        return CALL_OK;
      }
      bool descended = false;
      while (frame->cursor) {
        unsigned index = --frame->cursor;
        uint64_t block = npfs_get_u64(pool->io[0] + index * 8);
        uint64_t logical = frame->base + index * frame->span;
        if (!block || logical + frame->span <= limit) {
          continue;
        }
        for (unsigned i = 0; i < depth; i++) {
          if (frames[i].block == block) {
            return corrupt(context);
          }
        }
        frame->child_index = index;
        if (frame->level == 1) {
          path->target = block;
          path->root_slot = root_slot;
          path->depth = depth;
          for (unsigned i = 0; i < depth; i++) {
            path->parents[i] = frames[i].block;
            path->indices[i] = frames[i].child_index;
          }
          return CALL_OK;
        }
        frames[depth++] = (struct search_frame){block, logical,
          frame->span / NPFS_INDIRECT_COUNT, frame->level - 1, NPFS_INDIRECT_COUNT, 0};
        descended = true;
        break;
      }
      if (!descended) {
        depth--;
      }
    }
  }
  for (unsigned slot = NPFS_DIRECT_COUNT; slot; slot--) {
    unsigned index = slot - 1;
    if (index >= limit && pointers[index]) {
      path->target = pointers[index];
      path->root_slot = index;
      return CALL_OK;
    }
  }
  return CALL_OK;
}

static enum call_status remove_mapping(struct npfs_store_context *context, struct npfs_store_inode *inode,
  const struct cleanup_path *path)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  uint64_t freed = path->target;
  unsigned depth = path->depth;
  for (;;) {
    enum call_status status = bitmap_change(context, pool, freed, false);
    if (status != CALL_OK) {
      return status;
    }
    if (!depth) {
      inode->durable.pointers[path->root_slot] = 0;
      return CALL_OK;
    }
    unsigned index = --depth;
    uint8_t *bytes;
    status = edit_block(context, pool, path->parents[index], NPFS_METADATA_INDIRECT, false, &bytes);
    if (status != CALL_OK) {
      return status;
    }
    if (npfs_get_u64(bytes + path->indices[index] * 8) != freed) {
      return corrupt(context);
    }
    npfs_put_u64(bytes + path->indices[index] * 8, 0);
    bool empty = true;
    for (unsigned i = 0; i < NPFS_INDIRECT_COUNT; i++) {
      if (npfs_get_u64(bytes + i * 8)) {
        empty = false;
        break;
      }
    }
    if (!empty) {
      return CALL_OK;
    }
    freed = path->parents[index];
  }
}

static bool cleanup_mapping_fits(const struct npfs_store_pool *pool, const struct cleanup_path *path)
{
  uint64_t homes[7];
  unsigned count = 0;
  homes[count++] = pool->header.bitmap_start + path->target / NPFS_BITMAP_BITS;
  for (unsigned i = 0; i < path->depth; i++) {
    homes[count++] = path->parents[i];
    homes[count++] = pool->header.bitmap_start + path->parents[i] / NPFS_BITMAP_BITS;
  }
  unsigned images = pool->image_count - pool->operation_start;
  for (unsigned i = 0; i < count; i++) {
    bool present = false;
    for (unsigned j = 0; j < pool->image_count; j++) {
      present |= pool->images[j].home == homes[i];
    }
    for (unsigned j = 0; j < i; j++) {
      present |= homes[j] == homes[i];
    }
    if (!present) {
      images++;
    }
  }
  /* Reserve the inode image and the cleanup-list predecessor or volume image.
   * Ancestor bitmap images are included even when no ancestor becomes empty.
   * Each home may also need an undo copy when an earlier operation staged it. */
  return images <= STORE_CLEANUP_IMAGES - 2 &&
    pool->undo_count + count + 2 <= STORE_OPERATION_IMAGES;
}

static void drop_inode_cache(struct npfs_store_inode *inode, uint64_t first)
{
  struct npfs_store_pool *pool = inode->volume->pool;
  for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS; chunk++) {
    if (!pool->cache[chunk]) {
      continue;
    }
    for (unsigned i = 0; i < STORE_CHUNK_ENTRIES; i++) {
      struct store_cache_entry *entry = &pool->cache[chunk]->entries[i];
      if (entry->inode == inode && entry->logical >= first) {
        entry->inode = NULL;
        entry->dirty = false;
      }
    }
  }
}

static void discard_idle_inode(struct npfs_store_inode *inode, bool pressure)
{
  if (inode->references || inode->record.cleanup || inode->size_dirty ||
    inode->record.kind == NPFS_INODE_FREE || next_dirty(inode)) {
    return;
  }
  if (!pressure) {
    struct npfs_store_pool *pool = inode->volume->pool;
    for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS; chunk++) {
      if (!pool->cache[chunk]) {
        continue;
      }
      for (unsigned i = 0; i < STORE_CHUNK_ENTRIES; i++) {
        if (pool->cache[chunk]->entries[i].inode == inode) {
          return;
        }
      }
    }
  }
  drop_inode_cache(inode, 0);
  struct npfs_store_inode **link = &inode->volume->inodes;
  while (*link != inode) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = inode->next;
  store_free(inode);
}

static enum call_status cleanup_one(struct npfs_store_context *context, struct npfs_store_volume *volume)
{
  struct npfs_store_pool *pool = volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_store_inode *inode = NULL, *previous = NULL;
  uint64_t number = volume->record.cleanup_head;
  uint64_t count = volume->record.inode_bytes / NPFS_INODE_SIZE;
  while (number) {
    if (!count--) {
      return corrupt(context);
    }
    status = inode_get(context, volume, number, &inode);
    if (status != CALL_OK) {
      return status;
    }
    if (!inode->record.cleanup) {
      return corrupt(context);
    }
    if ((inode->record.cleanup & NPFS_CLEANUP_SHRINK) || !inode->references) {
      break;
    }
    previous = inode;
    number = inode->record.cleanup_next;
  }
  if (!number) {
    return CALL_OK;
  }
  if (inode->record.kind == NPFS_INODE_FILE &&
    ((inode->record.cleanup & NPFS_CLEANUP_DETACHED) != 0) != (inode->record.link_count == 0)) {
    return corrupt(context);
  }
  status = operation_begin(context, pool, STORE_CLEANUP_IMAGES, 0);
  if (status != CALL_OK) {
    return status;
  }
  bool detached = (inode->record.cleanup & NPFS_CLEANUP_DETACHED) &&
    !inode->record.link_count && !inode->references;
  uint64_t target = detached ? 0 : inode->record.shrink_target;
  uint64_t limit = target / NPFS_BLOCK_SIZE + (target % NPFS_BLOCK_SIZE != 0);
  struct npfs_inode old_record = inode->record, old_durable = inode->durable;
  struct npfs_inode previous_record = previous ? previous->record : (struct npfs_inode){0};
  struct npfs_volume old_volume = volume->record;
  bool reclaimed = false;
  bool complete = false;
  for (unsigned removed = 0; removed < STORE_CLEANUP_MAPPINGS; removed++) {
    struct cleanup_path path;
    status = highest_mapping(context, inode, limit, &path);
    if (status != CALL_OK) {
      break;
    }
    if (!path.target) {
      complete = true;
      break;
    }
    if (!cleanup_mapping_fits(pool, &path)) {
      break;
    }
    status = remove_mapping(context, inode, &path);
    if (status != CALL_OK) {
      break;
    }
    memcpy(inode->record.pointers, inode->durable.pointers, sizeof(inode->record.pointers));
  }
  if (status == CALL_OK && complete) {
    inode->record.cleanup &= ~NPFS_CLEANUP_SHRINK;
    inode->record.shrink_target = 0;
    inode->durable.cleanup &= ~NPFS_CLEANUP_SHRINK;
    inode->durable.shrink_target = 0;
    reclaimed = (inode->record.cleanup & NPFS_CLEANUP_DETACHED) &&
      !inode->record.link_count && !inode->references;
    if (!inode->record.cleanup || reclaimed) {
      uint64_t next = inode->record.cleanup_next;
      if (previous) {
        previous->record.cleanup_next = next;
        struct npfs_inode durable_previous = previous->durable;
        durable_previous.cleanup_next = next;
        status = stage_inode_record(context, previous, &durable_previous);
      } else {
        volume->record.cleanup_head = next;
        status = stage_volume(context, volume);
      }
      inode->record.cleanup_next = 0;
      inode->durable.cleanup_next = 0;
    }
    if (reclaimed) {
      memset(&inode->record, 0, sizeof(inode->record));
      memset(&inode->durable, 0, sizeof(inode->durable));
    }
  }
  if (status == CALL_OK) {
    status = stage_inode_record(context, inode, &inode->durable);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      inode->record = old_record;
      inode->durable = old_durable;
      volume->record = old_volume;
      if (previous) {
        previous->record = previous_record;
      }
    }
    operation_abort(pool);
    return status;
  }
  operation_end(pool);
  if (previous) {
    previous->durable.cleanup_next = previous->record.cleanup_next;
  }
  if (reclaimed) {
    /* Unlinked, unreferenced storage is deleted, including unpublished bytes.
     * Linked last-close and retained orphan handles never enter this path. */
    drop_inode_cache(inode, 0);
    inode->size_dirty = false;
    inode->durable_size = 0;
    inode->recycled_slot.number = inode->number;
    inode->recycled_slot.next = volume->free_slots;
    volume->free_slots = &inode->recycled_slot;
  }
  return CALL_OK;
}

static enum call_status zero_growth_tail(struct npfs_store_context *context, struct npfs_store_inode *inode)
{
  if (!inode->record.size || inode->record.size % NPFS_BLOCK_SIZE == 0) {
    return CALL_OK;
  }
  uint64_t logical = inode->record.size / NPFS_BLOCK_SIZE;
  struct store_cache_entry *entry = cache_find(inode, logical);
  if (!entry || !entry->dirty) {
    uint64_t physical;
    enum call_status status = map_read(context, inode->volume->pool,
      inode->record.pointers, logical, &physical);
    if (status != CALL_OK) {
      return status;
    }
    if (!physical) {
      return CALL_OK;
    }
  }
  enum call_status status = cache_get(context, inode, logical, &entry);
  if (status != CALL_OK) {
    return status;
  }
  unsigned offset = (unsigned)(inode->record.size % NPFS_BLOCK_SIZE);
  memset(entry->bytes + offset, 0, NPFS_BLOCK_SIZE - offset);
  entry->dirty = true;
  return CALL_OK;
}

enum call_status npfs_store_resize(struct npfs_store_context *context, struct npfs_store_inode *inode,
  uint64_t size)
{
  require_owner();
  struct npfs_store_pool *pool = inode->volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  if (inode->record.kind != NPFS_INODE_FILE) {
    return CALL_WRONG_TYPE;
  }
  if (size > NPFS_FILE_SIZE_MAX) {
    return CALL_FILE_TOO_LARGE;
  }
  status = completion_interruption(context);
  if (status != CALL_OK) {
    return status;
  }
  status = finish_shrink(context, inode);
  if (status != CALL_OK || size == inode->record.size) {
    return status;
  }
  if (size > inode->record.size) {
    status = zero_growth_tail(context, inode);
    if (status != CALL_OK) {
      return status;
    }
    inode->record.size = size;
    inode->size_dirty = true;
    touch_inode(context, &inode->record, false);
    return CALL_OK;
  }
  /* Preserve pending growth and retained partial-block writes before staging
   * the smaller size. Unrelated cached files are not written back here. */
  status = flush_inode(context, inode);
  if (status == CALL_OK) {
    status = begin_namespace(context, pool, false);
  }
  if (status != CALL_OK) {
    return status;
  }
  status = completion_interruption(context);
  if (status != CALL_OK) {
    operation_end(pool);
    return status;
  }
  struct npfs_inode old_record = inode->record;
  struct npfs_volume old_volume = inode->volume->record;
  inode->record.size = size;
  inode->record.shrink_target = size;
  touch_inode(context, &inode->record, false);
  status = cleanup_link(context, inode, NPFS_CLEANUP_SHRINK);
  if (status == CALL_OK) {
    status = stage_inode(context, inode);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      inode->record = old_record;
      inode->volume->record = old_volume;
    }
    operation_abort(pool);
    return status;
  }
  operation_end(pool);
  inode->durable = inode->record;
  inode->durable_size = size;
  inode->size_dirty = false;
  drop_inode_cache(inode, size / NPFS_BLOCK_SIZE + (size % NPFS_BLOCK_SIZE != 0));
  return CALL_OK;
}

bool npfs_store_pending(const struct npfs_store_pool *pool)
{
  require_owner();
  if (!pool->writable || pool->failed) {
    return false;
  }
  if (pool->control.state == NPFS_JOURNAL_COMMITTED) {
    return true;
  }
  for (const struct npfs_store_volume *volume = pool->volumes; volume; volume = volume->next) {
    for (const struct npfs_store_inode *inode = volume->inodes; inode; inode = inode->next) {
      if ((inode->record.cleanup & NPFS_CLEANUP_SHRINK) ||
        ((inode->record.cleanup & NPFS_CLEANUP_DETACHED) && !inode->references)) {
        return true;
      }
    }
  }
  return false;
}

bool npfs_store_journal_empty(const struct npfs_store_pool *pool)
{
  require_owner();
  return pool->control.state == NPFS_JOURNAL_EMPTY && !pool->image_count;
}

enum call_status npfs_store_maintain(struct npfs_store_context *context, struct npfs_store_pool *pool,
  bool flush_dirty, bool pressure)
{
  require_owner();
  enum call_status status = pool->failed ? pool->writeback_error : CALL_OK;
  if (pool->writable && !pool->failed && (flush_dirty || pressure)) {
    status = flush_files(context, pool);
    if (status == CALL_OK) {
      status = commit_running(context, pool);
    }
  }
  if (pressure) {
    store_vm_free(pool->write_run, STORE_WRITE_RUN_BLOCKS * NPFS_BLOCK_SIZE);
    pool->write_run = 0;
    for (unsigned chunk = 0; chunk < STORE_METADATA_CHUNKS; chunk++) {
      store_vm_free((uintptr_t)pool->metadata_cache[chunk], sizeof(struct store_metadata_chunk));
      pool->metadata_cache[chunk] = NULL;
    }
    memset(pool->metadata_homes, 0, sizeof(pool->metadata_homes));
    pool->metadata_next = 0;
    for (struct npfs_store_volume *volume = pool->volumes; volume; volume = volume->next) {
      struct npfs_store_inode *inode = volume->inodes;
      while (inode) {
        struct npfs_store_inode *next = inode->next;
        discard_idle_inode(inode, true);
        inode = next;
      }
    }
    for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS; chunk++) {
      if (!pool->cache[chunk]) {
        continue;
      }
      bool dirty = false;
      for (unsigned i = 0; i < STORE_CHUNK_ENTRIES; i++) {
        dirty |= pool->cache[chunk]->entries[i].dirty;
      }
      if (!dirty) {
        store_vm_free((uintptr_t)pool->cache[chunk], sizeof(struct store_cache_chunk));
        pool->cache[chunk] = NULL;
      }
    }
  }
  if (status != CALL_OK) {
    return status;
  }
  if (!pool->writable) {
    return CALL_OK;
  }
  if (pool->control.state == NPFS_JOURNAL_COMMITTED) {
    return checkpoint(context, pool);
  }
  for (struct npfs_store_volume *volume = pool->volumes; volume; volume = volume->next) {
    uint64_t number = volume->record.cleanup_head;
    uint64_t count = volume->record.inode_bytes / NPFS_INODE_SIZE;
    while (number) {
      if (!count--) {
        return corrupt(context);
      }
      struct npfs_store_inode *inode;
      status = inode_get(context, volume, number, &inode);
      if (status != CALL_OK) {
        return status;
      }
      if ((inode->record.cleanup & NPFS_CLEANUP_SHRINK) || !inode->references) {
        return cleanup_one(context, volume);
      }
      number = inode->record.cleanup_next;
    }
  }
  return CALL_OK;
}

void npfs_store_info(struct npfs_store_inode *inode, struct directory_filesystem_info *info)
{
  require_owner();
  struct npfs_store_pool *pool = inode->volume->pool;
  memset(info, 0, sizeof(*info));
  info->type = FILESYSTEM_TYPE_NPFS;
  info->flags = pool->writable ? 0 : FILESYSTEM_FLAG_READ_ONLY;
  if (pool->degraded) {
    info->flags |= FILESYSTEM_FLAG_DEGRADED;
  }
  memcpy(info->pool_id, pool->header.pool_id, NPFS_ID_SIZE);
  memcpy(info->volume_id, inode->volume->record.id, NPFS_ID_SIZE);
  info->generation = pool->control.sequence;
  info->pool_allocatable_bytes = (pool->header.pool_blocks - 2) * NPFS_BLOCK_SIZE;
  memcpy(info->volume_name, inode->volume->record.name, inode->volume->record.name_length);
}
