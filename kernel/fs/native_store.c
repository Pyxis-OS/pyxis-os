/* SPDX-License-Identifier: MPL-2.0 */
#include "native_store.h"

#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/task.h>

#define STORE_IMAGES_MAX 128u
#define STORE_NAMESPACE_IMAGES 18u
#define STORE_CACHE_CHUNKS 4u
#define STORE_CHUNK_ENTRIES 256u
#define STORE_CLEANUP_IMAGES 10u
#define STORE_CLEANUP_MAPPINGS 64u

_Static_assert(STORE_IMAGES_MAX <= PNF_DESCRIPTORS_PER_BLOCK);

struct store_free_slot {
  struct store_free_slot *next;
  uint64_t number;
  bool heap_owned;
};

struct native_store_volume {
  struct native_store_volume *next;
  struct native_store_pool *pool;
  struct pnf_volume record;
  unsigned slot;
  struct store_free_slot *free_slots;
  struct native_store_inode *inodes;
};

struct native_store_inode {
  struct native_store_inode *next;
  struct native_store_volume *volume;
  struct pnf_inode record, durable;
  uint64_t number, generation, durable_size;
  size_t references;
  bool size_dirty;
  struct store_free_slot recycled_slot;
};

struct store_cache_entry {
  struct native_store_inode *inode;
  uint64_t logical;
  bool dirty;
  uint8_t bytes[PNF_BLOCK_SIZE];
};

struct store_cache_chunk {
  struct store_cache_entry entries[STORE_CHUNK_ENTRIES];
};

struct store_image {
  uint64_t home;
  uint32_t kind;
};

struct native_store_pool {
  struct native_store_pool *next;
  struct gpt_partition partition;
  struct block_info device;
  struct pnf_header header;
  struct pnf_control control;
  unsigned control_slot;
  bool writable, degraded, failed;
  enum call_status writeback_error;
  uint64_t free_blocks, next_free;
  struct pnf_volume catalog[PNF_VOLUME_COUNT];
  struct native_store_volume *volumes;
  struct store_cache_chunk *cache[STORE_CACHE_CHUNKS];
  struct store_image images[STORE_IMAGES_MAX];
  uint32_t image_count, image_capacity;
  uintptr_t scratch;
  size_t scratch_bytes;
  /* Retained mount storage; journal images overlay this durable bitmap base. */
  uintptr_t bitmap;
  size_t bitmap_bytes;
  bool bitmap_loaded;
  uint8_t io[2][PNF_BLOCK_SIZE];
};

extern void nativefs_require_worker(void);

static struct native_store_pool *opened_pools;
static uint64_t next_generation;

static enum call_status checkpoint(struct native_store_context *, struct native_store_pool *);
static enum call_status flush_files(struct native_store_context *, struct native_store_pool *);
static enum call_status cleanup_one(struct native_store_context *, struct native_store_volume *);
static enum call_status zero_growth_tail(struct native_store_context *, struct native_store_inode *);
static void discard_idle_inode(struct native_store_inode *, bool);

static void require_owner(void)
{
  nativefs_require_worker();
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
  uint64_t flags = cpu_save_interrupts();
  enum mm_result result = vm_alloc(vm_kernel_space(), bytes, PAGE_SIZE, PAGE_WRITE, address);
  cpu_restore_interrupts(flags);
  return result == MM_OK ? CALL_OK : CALL_NO_MEMORY;
}

static void store_vm_free(uintptr_t address, size_t bytes)
{
  if (!address) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  KASSERT(vm_free(vm_kernel_space(), address, bytes) == MM_OK);
  cpu_restore_interrupts(flags);
}

static enum call_status format_failure(struct native_store_context *context, enum pnf_status status)
{
  if (context->format_error == PNF_OK) {
    context->format_error = status;
  }
  switch (status) {
    case PNF_OK: return CALL_OK;
    case PNF_INVALID: return CALL_BAD_REQUEST;
    case PNF_NOT_FOUND: return CALL_NOT_FOUND;
    case PNF_EXISTS: return CALL_ALREADY_EXISTS;
    case PNF_NO_MEMORY: return CALL_NO_MEMORY;
    case PNF_NO_SPACE: return CALL_NO_SPACE;
    case PNF_UNSUPPORTED: return CALL_UNAVAILABLE;
    case PNF_RECOVERY_REQUIRED: return CALL_READ_ONLY;
    default: return CALL_IO;
  }
}

static enum call_status corrupt(struct native_store_context *context)
{
  return format_failure(context, PNF_CORRUPT);
}

static enum call_status backing_failure(struct native_store_context *context, enum block_result result)
{
  if (context->backing_error == BLOCK_OK) {
    context->backing_error = result;
  }
  return result == BLOCK_TIMED_OUT ? CALL_TIMED_OUT : CALL_IO;
}

static enum call_status transport(struct native_store_context *context, struct native_store_pool *pool,
  enum block_operation operation, uint64_t first, uint32_t count, void *bytes)
{
  require_owner();
  uint64_t ratio = PNF_BLOCK_SIZE / pool->device.block_size;
  if (operation != BLOCK_FLUSH && (!count || first >= pool->header.pool_blocks ||
    count > pool->header.pool_blocks - first)) {
    return corrupt(context);
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
    enum block_result result = block_submit(operation, sector, transfer,
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

static uint8_t *image_bytes(struct native_store_pool *pool, unsigned index)
{
  return (uint8_t *)pool->scratch + (size_t)index * PNF_BLOCK_SIZE;
}

static const uint8_t *bitmap_page(struct native_store_pool *pool, uint64_t page)
{
  KASSERT(pool->bitmap_loaded && page < pool->header.bitmap_blocks);
  uint64_t home = pool->header.bitmap_start + page;
  for (unsigned i = 0; i < pool->image_count; i++) {
    if (pool->images[i].home == home) {
      return image_bytes(pool, i);
    }
  }
  return (const uint8_t *)pool->bitmap + (size_t)page * PNF_BLOCK_SIZE;
}

static enum call_status read_block(struct native_store_context *context, struct native_store_pool *pool,
  uint64_t home, void *bytes)
{
  for (unsigned i = 0; i < pool->image_count; i++) {
    if (pool->images[i].home == home) {
      memcpy(bytes, image_bytes(pool, i), PNF_BLOCK_SIZE);
      return CALL_OK;
    }
  }
  if (pool->bitmap_loaded && home >= pool->header.bitmap_start &&
    home - pool->header.bitmap_start < pool->header.bitmap_blocks) {
    memcpy(bytes, (const uint8_t *)pool->bitmap +
      (size_t)(home - pool->header.bitmap_start) * PNF_BLOCK_SIZE, PNF_BLOCK_SIZE);
    return CALL_OK;
  }
  return transport(context, pool, BLOCK_READ, home, 1, bytes);
}

static enum call_status edit_block(struct native_store_context *context, struct native_store_pool *pool,
  uint64_t home, uint32_t kind, bool fresh, uint8_t **out)
{
  for (unsigned i = 0; i < pool->image_count; i++) {
    if (pool->images[i].home == home) {
      if (pool->images[i].kind != kind) {
        return corrupt(context);
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
    memset(bytes, 0, PNF_BLOCK_SIZE);
  }
  else {
    status = read_block(context, pool, home, bytes);
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

static enum call_status allocated_block(struct native_store_context *context, struct native_store_pool *pool,
  uint64_t block)
{
  if (!pnf_data_block_valid(&pool->header, block)) {
    return corrupt(context);
  }
  const uint8_t *bytes = bitmap_page(pool, block / PNF_BITMAP_BITS);
  return bit_get(bytes, block % PNF_BITMAP_BITS) ? CALL_OK : corrupt(context);
}

static enum call_status map_read(struct native_store_context *context, struct native_store_pool *pool,
  const uint64_t pointers[PNF_POINTER_COUNT], uint64_t logical, uint64_t *physical)
{
  struct pnf_map_path path;
  enum pnf_status result = pnf_map_path(logical, &path);
  if (result != PNF_OK) {
    return format_failure(context, result);
  }
  uint64_t block = pointers[path.slot];
  for (unsigned level = 0; block && level < path.depth; level++) {
    enum call_status status = allocated_block(context, pool, block);
    if (status != CALL_OK) {
      return status;
    }
    status = read_block(context, pool, block, pool->io[0]);
    if (status != CALL_OK) {
      return status;
    }
    block = pnf_get_u64(pool->io[0] + path.index[level] * 8);
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

static enum call_status bitmap_change(struct native_store_context *context, struct native_store_pool *pool,
  uint64_t block, bool allocated)
{
  if (!pnf_data_block_valid(&pool->header, block)) {
    return corrupt(context);
  }
  uint8_t *bytes;
  enum call_status status = edit_block(context, pool,
    pool->header.bitmap_start + block / PNF_BITMAP_BITS, PNF_METADATA_BITMAP, false, &bytes);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t bit = block % PNF_BITMAP_BITS;
  if (bit_get(bytes, bit) == allocated) {
    return corrupt(context);
  }
  bit_set(bytes, bit, allocated);
  if (allocated) {
    if (!pool->free_blocks) {
      return corrupt(context);
    }
    pool->free_blocks--;
  } else {
    pool->free_blocks++;
  }
  return CALL_OK;
}

static enum call_status allocate_block(struct native_store_context *context, struct native_store_pool *pool,
  uint64_t *physical)
{
  if (!pool->free_blocks) {
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
      uint64_t page = block / PNF_BITMAP_BITS;
      uint64_t page_base = page * PNF_BITMAP_BITS;
      uint64_t end = page_base + PNF_BITMAP_BITS;
      if (end > ends[range]) {
        end = ends[range];
      }
      const uint8_t *bytes = bitmap_page(pool, page);
      while (block < end) {
        unsigned bit = (unsigned)(block % 64);
        uint64_t word_base = block - bit;
        uint64_t available = ~pnf_get_u64(bytes + (size_t)(word_base - page_base) / 8);
        available &= UINT64_MAX << bit;
        unsigned count = end - word_base < 64 ? (unsigned)(end - word_base) : 64;
        if (count < 64) {
          available &= (UINT64_C(1) << count) - 1;
        }
        if (available) {
          uint64_t candidate = word_base + (unsigned)__builtin_ctzll(available);
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

static enum call_status map_ensure(struct native_store_context *context, struct native_store_pool *pool,
  uint64_t pointers[PNF_POINTER_COUNT], uint64_t logical, uint32_t kind,
  uint64_t *physical)
{
  struct pnf_map_path path;
  if (pnf_map_path(logical, &path) != PNF_OK) {
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
        pnf_put_u64(parent + parent_offset, block);
      }
      else {
        *root = block;
      }
      if (level < path.depth || kind != 0) {
        uint8_t *fresh;
        status = edit_block(context, pool, block,
          level < path.depth ? PNF_METADATA_INDIRECT : kind, true, &fresh);
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
    status = edit_block(context, pool, block, PNF_METADATA_INDIRECT, false, &parent);
    if (status != CALL_OK) {
      return status;
    }
    parent_offset = path.index[level] * 8;
    block = pnf_get_u64(parent + parent_offset);
  }
  *physical = block;
  return CALL_OK;
}

static enum call_status stage_volume(struct native_store_context *context, struct native_store_volume *volume)
{
  uint8_t *bytes;
  enum call_status status = edit_block(context, volume->pool,
    volume->pool->header.volume_start + volume->slot / 8, PNF_METADATA_VOLUMES, false, &bytes);
  if (status != CALL_OK) {
    return status;
  }
  return format_failure(context, pnf_volume_encode(&volume->pool->header, &volume->record,
    bytes + (volume->slot % 8) * PNF_VOLUME_SIZE));
}

static enum call_status stage_inode_record(struct native_store_context *context, struct native_store_inode *inode,
  const struct pnf_inode *record)
{
  struct native_store_volume *volume = inode->volume;
  struct native_store_pool *pool = volume->pool;
  uint64_t offset = inode->number * PNF_INODE_SIZE;
  uint64_t block;
  enum call_status status = map_read(context, pool, volume->record.pointers,
    offset / PNF_BLOCK_SIZE, &block);
  if (status != CALL_OK) {
    return status;
  }
  if (!block) {
    return corrupt(context);
  }
  uint8_t *bytes;
  status = edit_block(context, pool, block, PNF_METADATA_INODES, false, &bytes);
  if (status != CALL_OK) {
    return status;
  }
  return format_failure(context, pnf_inode_encode(&pool->header, record,
    bytes + offset % PNF_BLOCK_SIZE));
}

static enum call_status stage_inode(struct native_store_context *context, struct native_store_inode *inode)
{
  return stage_inode_record(context, inode, &inode->record);
}

static void touch_inode(struct native_store_context *context, struct pnf_inode *inode, bool created)
{
  inode->flags &= ~PNF_TIME_MODIFIED_VALID;
  inode->modified_ns = 0;
  if (context->time_valid) {
    inode->flags |= PNF_TIME_MODIFIED_VALID;
    inode->modified_ns = context->time_ns;
  }
  if (created) {
    inode->flags &= ~PNF_TIME_CREATED_VALID;
    inode->created_ns = 0;
    if (context->time_valid) {
      inode->flags |= PNF_TIME_CREATED_VALID;
      inode->created_ns = context->time_ns;
    }
  }
}

static enum call_status writable(struct native_store_pool *pool)
{
  if (pool->failed) {
    return pool->writeback_error;
  }
  return pool->writable ? CALL_OK : CALL_READ_ONLY;
}

static enum call_status latch_failure(struct native_store_pool *pool, enum call_status status)
{
  /* A terminal failure supersedes a retained recoverable error. */
  if (!pool->failed) {
    pool->writeback_error = status;
  }
  pool->failed = true;
  return status;
}

static enum call_status commit(struct native_store_context *context, struct native_store_pool *pool)
{
  if (!pool->image_count) {
    return CALL_OK;
  }
  if (pool->control.sequence > UINT64_MAX - 2) {
    return CALL_LIMIT;
  }
  struct pnf_control control = pool->control;
  control.sequence++;
  control.state = PNF_JOURNAL_COMMITTED;
  control.image_count = pool->image_count;
  control.descriptor_blocks = (pool->image_count + PNF_DESCRIPTORS_PER_BLOCK - 1) /
  PNF_DESCRIPTORS_PER_BLOCK;
  uint8_t *descriptor_bytes = image_bytes(pool, pool->image_capacity);
  memset(descriptor_bytes, 0, PNF_BLOCK_SIZE);
  for (unsigned i = 0; i < pool->image_count; i++) {
    struct pnf_descriptor descriptor = { .home = pool->images[i].home,
      .kind = pool->images[i].kind };
    enum pnf_status result = pnf_descriptor_encode(&pool->header, &descriptor,
      descriptor_bytes + i * PNF_DESCRIPTOR_SIZE);
    if (result != PNF_OK) {
      return format_failure(context, result);
    }
  }
  uint32_t crc = pnf_payload_begin(&control);
  crc = pnf_crc_update(crc, descriptor_bytes, PNF_BLOCK_SIZE);
  for (unsigned i = 0; i < pool->image_count; i++) {
    crc = pnf_crc_update(crc, image_bytes(pool, i), PNF_BLOCK_SIZE);
  }
  control.payload_crc = pnf_crc_finish(crc);
  enum call_status status = transport(context, pool, BLOCK_WRITE,
    pool->header.journal_start + 2, 1, descriptor_bytes);
  for (unsigned i = 0; status == CALL_OK && i < pool->image_count; i++) {
    status = transport(context, pool, BLOCK_WRITE, pool->header.journal_start + 3 + i,
      1, image_bytes(pool, i));
  }
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  }
  if (status != CALL_OK) {
    return latch_failure(pool, status);
  }
  uint8_t *control_bytes = image_bytes(pool, pool->image_capacity + 1);
  enum pnf_status result = pnf_control_encode(&pool->header, &control, control_bytes);
  if (result != PNF_OK) {
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

static enum call_status checkpoint(struct native_store_context *context, struct native_store_pool *pool)
{
  if (pool->control.state == PNF_JOURNAL_EMPTY) {
    return CALL_OK;
  }
  if (pool->failed) {
    return pool->writeback_error;
  }
  if (pool->control.sequence == UINT64_MAX) {
    return latch_failure(pool, CALL_LIMIT);
  }
  for (unsigned i = 0; i < pool->image_count; i++) {
    enum call_status status = transport(context, pool, BLOCK_WRITE, pool->images[i].home,
      1, image_bytes(pool, i));
    if (status != CALL_OK) {
      return latch_failure(pool, status);
    }
  }
  enum call_status status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  if (status != CALL_OK) {
    return latch_failure(pool, status);
  }
  struct pnf_control empty = pool->control;
  empty.sequence++;
  empty.state = PNF_JOURNAL_EMPTY;
  empty.image_count = empty.descriptor_blocks = empty.payload_crc = 0;
  if (pnf_control_encode(&pool->header, &empty, pool->io[0]) != PNF_OK) {
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
    if (pool->images[i].kind == PNF_METADATA_BITMAP) {
      KASSERT(pool->bitmap_loaded);
      memcpy((uint8_t *)pool->bitmap +
        (size_t)(pool->images[i].home - pool->header.bitmap_start) * PNF_BLOCK_SIZE,
        image_bytes(pool, i), PNF_BLOCK_SIZE);
    }
  }
  pool->image_count = 0;
  return CALL_OK;
}

static enum call_status validate_log_image(struct native_store_context *context, struct native_store_pool *pool,
  const struct pnf_descriptor *descriptor, const uint8_t *bytes)
{
  if (descriptor->kind == PNF_METADATA_BITMAP) {
    uint64_t base = (descriptor->home - pool->header.bitmap_start) * PNF_BITMAP_BITS;
    for (unsigned bit = 0; bit < PNF_BITMAP_BITS; bit++) {
      uint64_t number = base + bit;
      if ((number >= pool->header.pool_blocks || !pnf_data_block_valid(&pool->header, number)) &&
        !bit_get(bytes, bit)) {
        return corrupt(context);
      }
    }
  } else if (descriptor->kind == PNF_METADATA_VOLUMES) {
    for (unsigned i = 0; i < 8; i++) {
      struct pnf_volume volume;
      enum pnf_status status = pnf_volume_decode(&pool->header, bytes + i * PNF_VOLUME_SIZE, &volume);
      if (status != PNF_OK) {
        return format_failure(context, status);
      }
    }
  } else if (descriptor->kind == PNF_METADATA_INODES) {
    for (unsigned i = 0; i < 16; i++) {
      struct pnf_inode inode;
      enum pnf_status status = pnf_inode_decode(&pool->header, bytes + i * PNF_INODE_SIZE, &inode);
      if (status != PNF_OK) {
        return format_failure(context, status);
      }
    }
  } else if (descriptor->kind == PNF_METADATA_DIRECTORY) {
    size_t offset = 0;
    while (offset < PNF_BLOCK_SIZE) {
      struct pnf_dirent entry;
      enum pnf_status status = pnf_dirent_decode(&pool->header, bytes + offset,
        PNF_BLOCK_SIZE - offset, &entry);
      if (status != PNF_OK) {
        return format_failure(context, status);
      }
      offset += entry.record_length;
    }
  } else if (descriptor->kind == PNF_METADATA_INDIRECT) {
    for (unsigned i = 0; i < PNF_INDIRECT_COUNT; i++) {
      uint64_t pointer = pnf_get_u64(bytes + i * 8);
      if (pointer && !pnf_data_block_valid(&pool->header, pointer)) {
        return corrupt(context);
      }
    }
  }
  return CALL_OK;
}

static enum call_status replay(struct native_store_context *context, struct native_store_pool *pool)
{
  if (pool->control.state == PNF_JOURNAL_EMPTY) {
    return CALL_OK;
  }
  if (!pool->writable) {
    return format_failure(context, PNF_RECOVERY_REQUIRED);
  }
  if (pool->control.sequence == UINT64_MAX) {
    return CALL_LIMIT;
  }
  uint64_t payload_blocks = (uint64_t)pool->control.descriptor_blocks + pool->control.image_count;
  if (payload_blocks > SIZE_MAX / PNF_BLOCK_SIZE) {
    return CALL_NO_MEMORY;
  }
  size_t payload_bytes = (size_t)payload_blocks * PNF_BLOCK_SIZE;
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
  uint32_t crc = pnf_payload_begin(&pool->control);
  for (uint64_t i = 0; i < payload_blocks; i++) {
    status = transport(context, pool, BLOCK_READ, pool->header.journal_start + 2 + i,
      1, payload + i * PNF_BLOCK_SIZE);
    if (status != CALL_OK) {
      goto done;
    }
    crc = pnf_crc_update(crc, payload + i * PNF_BLOCK_SIZE, PNF_BLOCK_SIZE);
  }
  if (pnf_crc_finish(crc) != pool->control.payload_crc) {
    status = corrupt(context);
    goto done;
  }
  size_t descriptors_bytes = (size_t)pool->control.descriptor_blocks * PNF_BLOCK_SIZE;
  for (unsigned i = 0; i < pool->control.image_count; i++) {
    struct pnf_descriptor descriptor;
    enum pnf_status result = pnf_descriptor_decode(&pool->header,
      payload + (size_t)i * PNF_DESCRIPTOR_SIZE, &descriptor);
    if (result != PNF_OK) {
      status = format_failure(context, result);
      goto done;
    }
    if (bit_get(targets, descriptor.home)) {
      status = corrupt(context);
      goto done;
    }
    bit_set(targets, descriptor.home, true);
    status = validate_log_image(context, pool, &descriptor,
      payload + descriptors_bytes + (size_t)i * PNF_BLOCK_SIZE);
    if (status != CALL_OK) {
      goto done;
    }
  }
  for (size_t i = (size_t)pool->control.image_count * PNF_DESCRIPTOR_SIZE;
    i < descriptors_bytes; i++) {
    if (payload[i]) {
      status = corrupt(context);
      goto done;
    }
  }
  for (unsigned i = 0; i < pool->control.image_count; i++) {
    uint64_t home = pnf_get_u64(payload + (size_t)i * PNF_DESCRIPTOR_SIZE);
    status = transport(context, pool, BLOCK_WRITE, home, 1,
      payload + descriptors_bytes + (size_t)i * PNF_BLOCK_SIZE);
    if (status != CALL_OK) {
      goto done;
    }
  }
  status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  if (status != CALL_OK) {
    goto done;
  }
  struct pnf_control empty = pool->control;
  empty.state = PNF_JOURNAL_EMPTY;
  empty.sequence++;
  empty.image_count = empty.descriptor_blocks = empty.payload_crc = 0;
  if (pnf_control_encode(&pool->header, &empty, pool->io[0]) != PNF_OK) {
    status = corrupt(context);
    goto done;
  }
  unsigned slot = pool->control_slot ^ 1;
  status = transport(context, pool, BLOCK_WRITE, pool->header.journal_start + slot, 1, pool->io[0]);
  if (status == CALL_OK) {
    status = transport(context, pool, BLOCK_FLUSH, 0, 0, NULL);
  }
  if (status == CALL_OK) {
    pool->control = empty;
    pool->control_slot = slot;
  }
  done:
  store_vm_free(targets_address, target_bytes);
  store_vm_free(payload_address, payload_bytes);
  return status;
}

static enum call_status prepare_writable(struct native_store_context *context, struct native_store_pool *pool)
{
  enum call_status status = format_failure(context, pnf_features_check(&pool->header, true));
  if (status != CALL_OK) {
    return status;
  }
  if (!pool->device.writable || !pool->device.flush_supported || pool->device.write_failed) {
    return CALL_READ_ONLY;
  }
  uint64_t capacity = pnf_journal_capacity(pool->header.journal_blocks);
  if (capacity < STORE_NAMESPACE_IMAGES) {
    return CALL_LIMIT;
  }
  pool->image_capacity = capacity < STORE_IMAGES_MAX ? (unsigned)capacity : STORE_IMAGES_MAX;
  pool->scratch_bytes = ((size_t)pool->image_capacity + 2) * PNF_BLOCK_SIZE;
  status = store_vm_allocate(pool->scratch_bytes, &pool->scratch);
  if (status == CALL_OK) {
    pool->writable = true;
  }
  return status;
}

enum call_status native_store_open(struct native_store_context *context,
  const struct gpt_partition *partition, const struct block_info *device,
  bool write, struct native_store_pool **out)
{
  require_owner();
  *out = NULL;
  if ((device->block_size != 512 && device->block_size != PNF_BLOCK_SIZE) ||
    device->max_transfer < device->block_size || !partition->block_count ||
    partition->first_block >= device->block_count ||
    partition->block_count > device->block_count - partition->first_block) {
    return CALL_UNAVAILABLE;
  }
  struct native_store_pool *pool = store_allocate(sizeof(*pool));
  if (!pool) {
    return CALL_NO_MEMORY;
  }
  pool->partition = *partition;
  pool->device = *device;
  pool->next_free = 1;
  pool->header.pool_blocks = partition->block_count / (PNF_BLOCK_SIZE / device->block_size);
  enum call_status status = CALL_IO;
  if (pool->header.pool_blocks < 2 || pool->header.pool_blocks > UINT64_MAX / PNF_BLOCK_SIZE) {
    goto fail;
  }
  uint64_t blocks = pool->header.pool_blocks;
  status = transport(context, pool, BLOCK_READ, 0, 1, pool->io[0]);
  if (status != CALL_OK) {
    goto fail;
  }
  status = transport(context, pool, BLOCK_READ, blocks - 1, 1, pool->io[1]);
  if (status != CALL_OK) {
    goto fail;
  }
  struct pnf_header headers[2];
  enum pnf_status states[2] = {
    pnf_header_decode(pool->io[0], &headers[0]),
    pnf_header_decode(pool->io[1], &headers[1]),
  };
  for (unsigned i = 0; i < 2; i++) {
    if (states[i] == PNF_UNSUPPORTED) {
      status = format_failure(context, states[i]);
      goto fail;
    }
    if (states[i] == PNF_OK && headers[i].pool_blocks != blocks) {
      states[i] = PNF_CORRUPT;
    }
  }
  if ((states[0] != PNF_OK && states[1] != PNF_OK) ||
    (states[0] == PNF_OK && states[1] == PNF_OK &&
    memcmp(pool->io[0], pool->io[1], PNF_BLOCK_SIZE))) {
    status = corrupt(context);
    goto fail;
  }
  pool->header = headers[states[0] == PNF_OK ? 0 : 1];
  pool->degraded = states[0] != PNF_OK || states[1] != PNF_OK;
  status = format_failure(context, pnf_features_check(&pool->header, write));
  if (status != CALL_OK) {
    goto fail;
  }
  for (struct native_store_pool *other = opened_pools; other; other = other->next) {
    if (!memcmp(pool->header.pool_id, other->header.pool_id, PNF_ID_SIZE)) {
      status = CALL_ALREADY_EXISTS;
      goto fail;
    }
  }
  struct pnf_control controls[2];
  for (unsigned i = 0; i < 2; i++) {
    status = transport(context, pool, BLOCK_READ, pool->header.journal_start + i, 1, pool->io[i]);
    if (status != CALL_OK) {
      goto fail;
    }
    states[i] = pnf_control_decode(&pool->header, pool->io[i], &controls[i]);
    if (states[i] != PNF_OK && pnf_control_checksum_valid(pool->io[i])) {
      status = format_failure(context, states[i]);
      goto fail;
    }
  }
  if ((states[0] != PNF_OK && states[1] != PNF_OK) ||
    (states[0] == PNF_OK && states[1] == PNF_OK && controls[0].sequence == controls[1].sequence &&
    memcmp(pool->io[0], pool->io[1], PNF_BLOCK_SIZE))) {
    status = corrupt(context);
    goto fail;
  }
  pool->control_slot = states[0] != PNF_OK ? 1 :
  states[1] != PNF_OK ? 0 : controls[1].sequence > controls[0].sequence ? 1 : 0;
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
  for (unsigned page = 0; page < PNF_VOLUME_TABLE_BLOCKS; page++) {
    status = read_block(context, pool, pool->header.volume_start + page, pool->io[0]);
    if (status != CALL_OK) {
      goto fail;
    }
    for (unsigned i = 0; i < 8; i++) {
      unsigned slot = page * 8 + i;
      enum pnf_status result = pnf_volume_decode(&pool->header,
        pool->io[0] + i * PNF_VOLUME_SIZE, &pool->catalog[slot]);
      if (result != PNF_OK) {
        status = format_failure(context, result);
        goto fail;
      }
      if (pool->catalog[slot].state != PNF_VOLUME_LIVE) {
        continue;
      }
      for (unsigned j = 0; j < slot; j++) {
        struct pnf_volume *a = &pool->catalog[slot], *b = &pool->catalog[j];
        if (b->state == PNF_VOLUME_LIVE && (!memcmp(a->id, b->id, PNF_ID_SIZE) ||
          (a->name_length == b->name_length && !memcmp(a->name, b->name, a->name_length)))) {
          status = corrupt(context);
          goto fail;
        }
      }
    }
  }
  if (pool->header.bitmap_blocks > SIZE_MAX / PNF_BLOCK_SIZE) {
    status = CALL_LIMIT;
    goto fail;
  }
  pool->bitmap_bytes = (size_t)pool->header.bitmap_blocks * PNF_BLOCK_SIZE;
  status = store_vm_allocate(pool->bitmap_bytes, &pool->bitmap);
  if (status != CALL_OK) {
    goto fail;
  }
  for (uint64_t page = 0; page < pool->header.bitmap_blocks;) {
    uint64_t remaining = pool->header.bitmap_blocks - page;
    uint32_t count = remaining < 128 ? (uint32_t)remaining : 128;
    status = transport(context, pool, BLOCK_READ, pool->header.bitmap_start + page,
      count, (uint8_t *)pool->bitmap + (size_t)page * PNF_BLOCK_SIZE);
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
    const uint8_t *bytes = (const uint8_t *)pool->bitmap + (size_t)page * PNF_BLOCK_SIZE;
    for (unsigned bit = 0; bit < PNF_BITMAP_BITS; bit++) {
      uint64_t number = page * PNF_BITMAP_BITS + bit;
      bool fixed = number >= pool->header.pool_blocks || !pnf_data_block_valid(&pool->header, number);
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

enum call_status native_store_upgrade(struct native_store_context *context, struct native_store_pool *pool)
{
  require_owner();
  if (pool->writable) {
    return writable(pool);
  }
  return prepare_writable(context, pool);
}

const uint8_t *native_store_pool_id(const struct native_store_pool *pool)
{
  return pool->header.pool_id;
}

bool native_store_writable(const struct native_store_pool *pool)
{
  return pool->writable && !pool->failed;
}

static enum call_status inode_record_read(struct native_store_context *context, struct native_store_volume *volume,
  uint64_t number, struct pnf_inode *record)
{
  if (number >= volume->record.inode_bytes / PNF_INODE_SIZE) {
    return corrupt(context);
  }
  uint64_t offset = number * PNF_INODE_SIZE;
  uint64_t block;
  enum call_status status = map_read(context, volume->pool, volume->record.pointers,
    offset / PNF_BLOCK_SIZE, &block);
  if (status != CALL_OK) {
    return status;
  }
  if (!block) {
    return corrupt(context);
  }
  status = read_block(context, volume->pool, block, volume->pool->io[0]);
  if (status != CALL_OK) {
    return status;
  }
  return format_failure(context, pnf_inode_decode(&volume->pool->header,
    volume->pool->io[0] + offset % PNF_BLOCK_SIZE, record));
}

static enum call_status inode_get(struct native_store_context *context, struct native_store_volume *volume,
  uint64_t number, struct native_store_inode **out)
{
  for (struct native_store_inode *inode = volume->inodes; inode; inode = inode->next) {
    if (inode->number == number) {
      *out = inode;
      return CALL_OK;
    }
  }
  struct native_store_inode *inode = store_allocate(sizeof(*inode));
  if (!inode) {
    return CALL_NO_MEMORY;
  }
  enum call_status status = inode_record_read(context, volume, number, &inode->record);
  if (status != CALL_OK) {
    store_free(inode);
    return status;
  }
  if (next_generation == UINT64_MAX) {
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

static void free_volume_storage(struct native_store_volume *volume)
{
  while (volume->free_slots) {
    struct store_free_slot *next = volume->free_slots->next;
    if (volume->free_slots->heap_owned) {
      store_free(volume->free_slots);
    }
    volume->free_slots = next;
  }
  while (volume->inodes) {
    struct native_store_inode *next = volume->inodes->next;
    store_free(volume->inodes);
    volume->inodes = next;
  }
  store_free(volume);
}

static enum call_status volume_mount(struct native_store_context *context, struct native_store_pool *pool,
  unsigned slot, struct native_store_volume **out)
{
  for (struct native_store_volume *volume = pool->volumes; volume; volume = volume->next) {
    if (volume->slot == slot) {
      *out = volume;
      return CALL_OK;
    }
  }
  struct native_store_volume *volume = store_allocate(sizeof(*volume));
  if (!volume) {
    return CALL_NO_MEMORY;
  }
  volume->pool = pool;
  volume->slot = slot;
  volume->record = pool->catalog[slot];
  enum call_status status = CALL_OK;
  uint64_t count = volume->record.inode_bytes / PNF_INODE_SIZE;
  uint64_t cleanup_count = 0;
  for (uint64_t number = 0; number < count; number++) {
    struct pnf_inode record;
    status = inode_record_read(context, volume, number, &record);
    if (status != CALL_OK) {
      goto fail;
    }
    if ((!number && record.kind != PNF_INODE_FREE) ||
      (number == 1 && (record.kind != PNF_INODE_DIRECTORY || record.parent != 1 || (record.cleanup & PNF_CLEANUP_DETACHED)))) {
      status = corrupt(context);
      goto fail;
    }
    if (record.cleanup) {
      cleanup_count++;
    }
    if (number && record.kind == PNF_INODE_FREE) {
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
    struct native_store_inode *inode;
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

enum call_status native_store_root(struct native_store_context *context, struct native_store_pool *pool,
  const char *name, size_t length, struct native_store_inode **out)
{
  require_owner();
  *out = NULL;
  for (unsigned slot = 0; slot < PNF_VOLUME_COUNT; slot++) {
    struct pnf_volume *record = &pool->catalog[slot];
    if (record->state != PNF_VOLUME_LIVE || record->name_length != length ||
      memcmp(record->name, name, length)) {
      continue;
    }
    struct native_store_volume *volume;
    enum call_status status = volume_mount(context, pool, slot, &volume);
    if (status != CALL_OK) {
      return status;
    }
    status = inode_get(context, volume, 1, out);
    if (status == CALL_OK) {
      native_store_retain(*out);
    }
    return status;
  }
  return CALL_NOT_FOUND;
}

void native_store_retain(struct native_store_inode *inode)
{
  require_owner();
  KASSERT(inode->references != SIZE_MAX);
  inode->references++;
}

void native_store_release(struct native_store_inode *inode)
{
  require_owner();
  KASSERT(inode->references);
  inode->references--;
  discard_idle_inode(inode, false);
}

uint64_t native_store_kind(const struct native_store_inode *inode)
{
  return inode->record.kind == PNF_INODE_DIRECTORY ? DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE;
}

uint64_t native_store_size(const struct native_store_inode *inode)
{
  return inode->record.size;
}

bool native_store_same_volume(const struct native_store_inode *a, const struct native_store_inode *b)
{
  return a->volume == b->volume;
}

struct native_store_pool *native_store_inode_pool(struct native_store_inode *inode)
{
  return inode->volume->pool;
}

static enum call_status directory_record(struct native_store_context *context, struct native_store_inode *directory,
  uint64_t position, struct pnf_dirent *entry)
{
  struct native_store_pool *pool = directory->volume->pool;
  uint64_t physical;
  enum call_status status = map_read(context, pool, directory->record.pointers,
    position / PNF_BLOCK_SIZE, &physical);
  if (status != CALL_OK) {
    return status;
  }
  if (!physical) {
    return corrupt(context);
  }
  status = read_block(context, pool, physical, pool->io[0]);
  if (status != CALL_OK) {
    return status;
  }
  unsigned offset = (unsigned)(position % PNF_BLOCK_SIZE);
  return format_failure(context, pnf_dirent_decode(&pool->header, pool->io[0] + offset,
    PNF_BLOCK_SIZE - offset, entry));
}

static enum call_status directory_find(struct native_store_context *context, struct native_store_inode *directory,
  const char *name, size_t length, struct pnf_dirent *found, uint64_t *position)
{
  if (directory->record.kind != PNF_INODE_DIRECTORY) {
    return CALL_WRONG_TYPE;
  }
  if (!pnf_name_valid((const uint8_t *)name, length)) {
    return CALL_BAD_REQUEST;
  }
  bool match = false;
  uint64_t offset = 0;
  while (offset < directory->record.size) {
    struct pnf_dirent entry;
    enum call_status status = directory_record(context, directory, offset, &entry);
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

static bool valid_child(const struct native_store_inode *directory,
  const struct native_store_inode *child)
{
  return child->number != 1 && child->record.kind != PNF_INODE_FREE &&
    !(child->record.cleanup & PNF_CLEANUP_DETACHED) &&
    (child->record.kind != PNF_INODE_DIRECTORY || child->record.parent == directory->number);
}

enum call_status native_store_lookup(struct native_store_context *context, struct native_store_inode *directory,
  const char *name, size_t length, struct native_store_inode **out)
{
  require_owner();
  *out = NULL;
  struct pnf_dirent entry;
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
  native_store_retain(*out);
  return CALL_OK;
}

enum call_status native_store_enumerate(struct native_store_context *context, struct native_store_inode *directory,
  uint64_t generation, uint64_t position, struct pnf_dirent *entry,
  uint64_t *next_position, uint64_t *current_generation)
{
  require_owner();
  memset(entry, 0, sizeof(*entry));
  *current_generation = directory->generation;
  *next_position = position;
  if (directory->record.kind != PNF_INODE_DIRECTORY) {
    return CALL_WRONG_TYPE;
  }
  if (generation && generation != directory->generation) {
    return CALL_OK;
  }
  if ((!generation && position) || position > directory->record.size) {
    return CALL_BAD_REQUEST;
  }
  if (position < directory->record.size) {
    uint64_t boundary = position - position % PNF_BLOCK_SIZE;
    while (boundary < position) {
      struct pnf_dirent previous;
      enum call_status status = directory_record(context, directory, boundary, &previous);
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
    enum call_status status = directory_record(context, directory, position, entry);
    if (status != CALL_OK) {
      return status;
    }
    position += entry->record_length;
    if (entry->inode) {
      struct native_store_inode *target;
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

static struct store_cache_entry *cache_find(struct native_store_inode *inode, uint64_t logical)
{
  struct native_store_pool *pool = inode->volume->pool;
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

static enum call_status cache_get(struct native_store_context *context, struct native_store_inode *inode,
  uint64_t logical, struct store_cache_entry **out)
{
  *out = cache_find(inode, logical);
  if (*out) {
    return CALL_OK;
  }
  struct native_store_pool *pool = inode->volume->pool;
  struct store_cache_entry *slot = NULL;
  for (unsigned chunk = 0; chunk < STORE_CACHE_CHUNKS && !slot; chunk++) {
    if (!pool->cache[chunk]) {
      uintptr_t address;
      enum call_status status = store_vm_allocate(sizeof(struct store_cache_chunk), &address);
      if (status != CALL_OK) {
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
    return CALL_NO_MEMORY;
  }
  struct native_store_inode *previous_owner = slot->inode;
  slot->inode = NULL;
  slot->dirty = false;
  if (previous_owner && previous_owner != inode) {
    discard_idle_inode(previous_owner, false);
  }
  uint64_t physical;
  enum call_status status = map_read(context, pool, inode->record.pointers, logical, &physical);
  if (status != CALL_OK) {
    return status;
  }
  if (physical) {
    status = read_block(context, pool, physical, slot->bytes);
  }
  else {
    memset(slot->bytes, 0, PNF_BLOCK_SIZE);
  }
  if (status != CALL_OK) {
    return status;
  }
  slot->inode = inode;
  slot->logical = logical;
  slot->dirty = false;
  *out = slot;
  return CALL_OK;
}

enum call_status native_store_read(struct native_store_context *context, struct native_store_inode *inode,
  uint64_t offset, void *bytes, size_t capacity, size_t *read)
{
  require_owner();
  *read = 0;
  if (inode->record.kind != PNF_INODE_FILE) {
    return CALL_WRONG_TYPE;
  }
  if (offset >= inode->record.size) {
    return CALL_OK;
  }
  uint64_t remaining = inode->record.size - offset;
  size_t length = remaining < capacity ? (size_t)remaining : capacity;
  uint8_t *destination = bytes;
  while (length) {
    unsigned within = (unsigned)(offset % PNF_BLOCK_SIZE);
    size_t count = PNF_BLOCK_SIZE - within;
    if (count > length) {
      count = length;
    }
    struct store_cache_entry *entry;
    enum call_status status = cache_get(context, inode, offset / PNF_BLOCK_SIZE, &entry);
    if (status != CALL_OK) {
      return status;
    }
    memcpy(destination, entry->bytes + within, count);
    destination += count;
    offset += count;
    length -= count;
    *read += count;
  }
  return CALL_OK;
}

static struct store_cache_entry *next_dirty(struct native_store_inode *inode)
{
  struct native_store_pool *pool = inode->volume->pool;
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

static enum call_status flush_inode(struct native_store_context *context, struct native_store_inode *inode)
{
  struct native_store_pool *pool = inode->volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  while (inode->size_dirty || next_dirty(inode)) {
    status = checkpoint(context, pool);
    if (status != CALL_OK) {
      return status;
    }
    struct pnf_inode previous = inode->record;
    uint64_t previous_free = pool->free_blocks;
    struct store_cache_entry *written[STORE_IMAGES_MAX];
    unsigned written_count = 0;
    struct store_cache_entry *entry = next_dirty(inode);
    while (entry && pool->image_capacity - pool->image_count >= 10 && written_count < STORE_IMAGES_MAX) {
      uint64_t physical;
      status = map_ensure(context, pool, inode->record.pointers, entry->logical, 0, &physical);
      if (status != CALL_OK) {
        break;
      }
      status = transport(context, pool, BLOCK_WRITE, physical, 1, entry->bytes);
      if (status != CALL_OK) {
        latch_failure(pool, status);
        break;
      }
      entry->dirty = false;
      written[written_count++] = entry;
      entry = next_dirty(inode);
    }
    if (status == CALL_OK) {
      struct pnf_inode durable = inode->record;
      uint64_t prefix = entry ? entry->logical * PNF_BLOCK_SIZE : inode->record.size;
      if (prefix < inode->durable_size) {
        prefix = inode->durable_size;
      }
      durable.size = prefix;
      if (durable.cleanup & PNF_CLEANUP_SHRINK) {
        durable.shrink_target = prefix;
      }
      status = stage_inode_record(context, inode, &durable);
      if (status == CALL_OK) {
        status = commit(context, pool);
      }
      if (status == CALL_OK) {
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
      pool->free_blocks = previous_free;
      if (!pool->failed && pool->control.state == PNF_JOURNAL_EMPTY) {
        pool->image_count = 0;
      }
      if (pool->writeback_error == CALL_OK) {
        pool->writeback_error = status;
      }
      return status;
    }
    kernel_task_yield_if_runnable();
  }
  return CALL_OK;
}

static enum call_status flush_files(struct native_store_context *context, struct native_store_pool *pool)
{
  if (pool->failed) {
    return pool->writeback_error;
  }
  for (struct native_store_volume *volume = pool->volumes; volume; volume = volume->next) {
    for (struct native_store_inode *inode = volume->inodes; inode; inode = inode->next) {
      if (inode->record.kind != PNF_INODE_FILE ||
        (!inode->references && inode->record.cleanup & PNF_CLEANUP_DETACHED)) {
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

static enum call_status finish_shrink(struct native_store_context *context, struct native_store_inode *inode)
{
  while (inode->record.cleanup & PNF_CLEANUP_SHRINK) {
    enum call_status status = cleanup_one(context, inode->volume);
    if (status != CALL_OK) {
      return status;
    }
    kernel_task_yield_if_runnable();
  }
  return CALL_OK;
}

enum call_status native_store_write(struct native_store_context *context, struct native_store_inode *inode,
  uint64_t offset, const void *bytes, size_t length, size_t *written)
{
  require_owner();
  *written = 0;
  enum call_status status = writable(inode->volume->pool);
  if (status != CALL_OK) {
    return status;
  }
  if (inode->record.kind != PNF_INODE_FILE) {
    return CALL_WRONG_TYPE;
  }
  if (offset > PNF_FILE_SIZE_MAX || length > PNF_FILE_SIZE_MAX - offset) {
    return CALL_FILE_TOO_LARGE;
  }
  if (!length) {
    return CALL_OK;
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
    unsigned within = (unsigned)(offset % PNF_BLOCK_SIZE);
    size_t count = PNF_BLOCK_SIZE - within;
    if (count > length) {
      count = length;
    }
    struct store_cache_entry *entry;
    status = cache_get(context, inode, offset / PNF_BLOCK_SIZE, &entry);
    if (status != CALL_OK) {
      return *written ? CALL_OK : status;
    }
    uint64_t old_size = inode->record.size;
    uint64_t base = offset - within;
    if (old_size < base + PNF_BLOCK_SIZE) {
      size_t start = old_size > base ? (size_t)(old_size - base) : 0;
      memset(entry->bytes + start, 0, PNF_BLOCK_SIZE - start);
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

enum call_status native_store_sync(struct native_store_context *context, struct native_store_pool *pool)
{
  require_owner();
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  status = flush_files(context, pool);
  if (!pool->failed) {
    if (status == CALL_OK) {
      status = pool->writeback_error;
    }
    pool->writeback_error = CALL_OK;
  }
  return status;
}

static enum call_status begin_namespace(struct native_store_context *context, struct native_store_pool *pool, bool flush)
{
  enum call_status status = writable(pool);
  if (status == CALL_OK && flush) {
    status = flush_files(context, pool);
  }
  if (status == CALL_OK) {
    status = checkpoint(context, pool);
  }
  if (status == CALL_OK && next_generation > UINT64_MAX - 4) {
    status = CALL_LIMIT;
  }
  return status;
}

static enum call_status directory_edit(struct native_store_context *context, struct native_store_inode *directory,
  uint64_t position, uint8_t **bytes)
{
  uint64_t physical;
  enum call_status status = map_read(context, directory->volume->pool, directory->record.pointers,
    position / PNF_BLOCK_SIZE, &physical);
  if (status != CALL_OK) {
    return status;
  }
  if (!physical) {
    return corrupt(context);
  }
  return edit_block(context, directory->volume->pool, physical, PNF_METADATA_DIRECTORY, false, bytes);
}

static enum call_status directory_clear(struct native_store_context *context, struct native_store_inode *directory,
  uint64_t position, uint16_t record_length)
{
  uint8_t *bytes;
  enum call_status status = directory_edit(context, directory, position, &bytes);
  if (status != CALL_OK) {
    return status;
  }
  struct pnf_dirent free_entry = { .record_length = record_length };
  return format_failure(context, pnf_dirent_encode(&directory->volume->pool->header,
    &free_entry, bytes + position % PNF_BLOCK_SIZE));
}

static enum call_status directory_insert(struct native_store_context *context, struct native_store_inode *directory,
  const char *name, size_t length, uint64_t number)
{
  if (directory->record.cleanup & PNF_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  uint16_t needed = (uint16_t)((PNF_DIRENT_HEADER_SIZE + length + 7) & ~7u);
  uint64_t position = 0;
  struct pnf_dirent free_entry = {0};
  bool available = false;
  while (position < directory->record.size) {
    enum call_status status = directory_record(context, directory, position, &free_entry);
    if (status != CALL_OK) {
      return status;
    }
    if (!free_entry.inode && free_entry.record_length >= needed) {
      available = true;
      break;
    }
    position += free_entry.record_length;
  }
  struct native_store_pool *pool = directory->volume->pool;
  uint8_t *bytes;
  enum call_status status;
  if (!available) {
    if (directory->record.size > PNF_FILE_SIZE_MAX - PNF_BLOCK_SIZE) {
      return CALL_FILE_TOO_LARGE;
    }
    uint64_t physical;
    position = directory->record.size;
    status = map_ensure(context, pool, directory->record.pointers,
      position / PNF_BLOCK_SIZE, PNF_METADATA_DIRECTORY, &physical);
    if (status != CALL_OK) {
      return status;
    }
    status = edit_block(context, pool, physical, PNF_METADATA_DIRECTORY, false, &bytes);
    if (status != CALL_OK) {
      return status;
    }
    free_entry.record_length = PNF_BLOCK_SIZE;
    directory->record.size += PNF_BLOCK_SIZE;
  } else {
    status = directory_edit(context, directory, position, &bytes);
    if (status != CALL_OK) {
      return status;
    }
  }
  uint16_t remaining = free_entry.record_length - needed;
  struct pnf_dirent entry = {
    .inode = number,
    .record_length = remaining >= PNF_DIRENT_HEADER_SIZE ? needed : free_entry.record_length,
    .name_length = (uint16_t)length,
  };
  memcpy(entry.name, name, length);
  unsigned offset = (unsigned)(position % PNF_BLOCK_SIZE);
  status = format_failure(context, pnf_dirent_encode(&pool->header, &entry, bytes + offset));
  if (status == CALL_OK && remaining >= PNF_DIRENT_HEADER_SIZE) {
    struct pnf_dirent tail = { .record_length = remaining };
    status = format_failure(context, pnf_dirent_encode(&pool->header, &tail, bytes + offset + needed));
  }
  return status;
}

static enum call_status directory_empty(struct native_store_context *context, struct native_store_inode *directory)
{
  for (uint64_t position = 0; position < directory->record.size;) {
    struct pnf_dirent entry;
    enum call_status status = directory_record(context, directory, position, &entry);
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

static enum call_status cleanup_link(struct native_store_context *context, struct native_store_inode *inode, uint32_t flag)
{
  struct native_store_volume *volume = inode->volume;
  if (!inode->record.cleanup) {
    inode->record.cleanup_next = volume->record.cleanup_head;
    volume->record.cleanup_head = inode->number;
  }
  inode->record.cleanup |= flag;
  if (flag & PNF_CLEANUP_DETACHED) {
    inode->record.parent = 0;
  }
  return stage_volume(context, volume);
}

static void namespace_abort(struct native_store_pool *pool, uint64_t free_blocks)
{
  if (!pool->failed && pool->control.state == PNF_JOURNAL_EMPTY) {
    pool->image_count = 0;
    pool->free_blocks = free_blocks;
  }
}

enum call_status native_store_create(struct native_store_context *context, struct native_store_inode *directory,
  const char *name, size_t length, uint64_t kind, struct native_store_inode **out)
{
  require_owner();
  *out = NULL;
  if (directory->record.kind != PNF_INODE_DIRECTORY) {
    return CALL_WRONG_TYPE;
  }
  if (!pnf_name_valid((const uint8_t *)name, length) ||
    (kind != DIRECTORY_KIND_FILE && kind != DIRECTORY_KIND_DIRECTORY)) {
    return CALL_BAD_REQUEST;
  }
  if (directory->record.cleanup & PNF_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  struct native_store_volume *volume = directory->volume;
  struct native_store_pool *pool = volume->pool;
  enum call_status status = begin_namespace(context, pool, true);
  if (status != CALL_OK) {
    return status;
  }
  struct pnf_dirent existing;
  uint64_t position;
  status = directory_find(context, directory, name, length, &existing, &position);
  if (status != CALL_NOT_FOUND) {
    return status == CALL_OK ? CALL_ALREADY_EXISTS : status;
  }
  struct pnf_inode parent_previous = directory->record;
  struct pnf_volume volume_previous = volume->record;
  uint64_t free_previous = pool->free_blocks;
  struct store_free_slot *prepared_slots = NULL;
  struct store_free_slot *selected = volume->free_slots;
  uint64_t number;
  if (selected) {
    number = selected->number;
  } else {
    uint64_t old_bytes = volume->record.inode_bytes;
    uint64_t new_bytes = (old_bytes + PNF_BLOCK_SIZE) & ~(uint64_t)(PNF_BLOCK_SIZE - 1);
    if (new_bytes > PNF_FILE_SIZE_MAX) {
      return CALL_FILE_TOO_LARGE;
    }
    number = old_bytes / PNF_INODE_SIZE;
    for (uint64_t slot = number + 1; slot < new_bytes / PNF_INODE_SIZE; slot++) {
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
      old_bytes / PNF_BLOCK_SIZE, PNF_METADATA_INODES, &physical);
    if (status != CALL_OK) {
      goto fail;
    }
    uint8_t *bytes;
    status = edit_block(context, pool, physical, PNF_METADATA_INODES, false, &bytes);
    if (status != CALL_OK) {
      goto fail;
    }
    memset(bytes + old_bytes % PNF_BLOCK_SIZE, 0, PNF_BLOCK_SIZE - old_bytes % PNF_BLOCK_SIZE);
    volume->record.inode_bytes = new_bytes;
    status = stage_volume(context, volume);
    if (status != CALL_OK) {
      goto fail;
    }
  }
  struct native_store_inode *child;
  status = inode_get(context, volume, number, &child);
  if (status != CALL_OK) {
    goto fail;
  }
  if (child->record.kind != PNF_INODE_FREE || child->references) {
    status = corrupt(context);
    goto fail;
  }
  child->record = (struct pnf_inode){
    .kind = kind == DIRECTORY_KIND_DIRECTORY ? PNF_INODE_DIRECTORY : PNF_INODE_FILE,
    .mapping = PNF_MAPPING_POINTERS,
    .parent = kind == DIRECTORY_KIND_DIRECTORY ? directory->number : 0,
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
  if (status == CALL_OK) {
    status = commit(context, pool);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      memset(&child->record, 0, sizeof(child->record));
    }
    goto fail;
  }
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
  child->generation = ++next_generation;
  directory->durable = directory->record;
  directory->durable_size = directory->record.size;
  directory->generation = ++next_generation;
  native_store_retain(child);
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
  namespace_abort(pool, free_previous);
  return status;
}

enum call_status native_store_remove(struct native_store_context *context, struct native_store_inode *directory,
  const char *name, size_t length, uint64_t kind)
{
  require_owner();
  if (directory->record.cleanup & PNF_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  struct native_store_pool *pool = directory->volume->pool;
  enum call_status status = begin_namespace(context, pool, false);
  if (status != CALL_OK) {
    return status;
  }
  struct pnf_dirent entry;
  uint64_t position;
  status = directory_find(context, directory, name, length, &entry, &position);
  if (status != CALL_OK) {
    return status;
  }
  struct native_store_inode *target;
  status = inode_get(context, directory->volume, entry.inode, &target);
  if (status != CALL_OK) {
    return status;
  }
  if (!valid_child(directory, target)) {
    discard_idle_inode(target, false);
    return corrupt(context);
  }
  if (kind != DIRECTORY_KIND_ANY && kind != native_store_kind(target)) {
    discard_idle_inode(target, false);
    return CALL_WRONG_TYPE;
  }
  if (target->record.kind == PNF_INODE_DIRECTORY) {
    status = directory_empty(context, target);
    if (status != CALL_OK) {
      discard_idle_inode(target, false);
      return status;
    }
  }
  struct pnf_inode parent_previous = directory->record;
  struct pnf_inode target_previous = target->record;
  struct pnf_volume volume_previous = directory->volume->record;
  uint64_t free_previous = pool->free_blocks;
  status = directory_clear(context, directory, position, entry.record_length);
  if (status == CALL_OK) {
    status = cleanup_link(context, target, PNF_CLEANUP_DETACHED);
  }
  touch_inode(context, &directory->record, false);
  struct pnf_inode detached = target->durable;
  detached.cleanup = target->record.cleanup;
  detached.cleanup_next = target->record.cleanup_next;
  detached.parent = 0;
  if (status == CALL_OK) {
    status = stage_inode_record(context, target, &detached);
  }
  if (status == CALL_OK) {
    status = stage_inode(context, directory);
  }
  if (status == CALL_OK) {
    status = commit(context, pool);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      directory->record = parent_previous;
      target->record = target_previous;
      directory->volume->record = volume_previous;
    }
    namespace_abort(pool, free_previous);
    discard_idle_inode(target, false);
    return status;
  }
  target->durable = detached;
  directory->durable = directory->record;
  directory->generation = ++next_generation;
  return CALL_OK;
}

enum call_status native_store_rename(struct native_store_context *context, struct native_store_inode *source,
  const char *source_name, size_t source_length,
  struct native_store_inode *destination, const char *destination_name,
  size_t destination_length, bool replace)
{
  require_owner();
  if (source->volume != destination->volume) {
    return CALL_BAD_REQUEST;
  }
  if (source->record.cleanup & PNF_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  if (!pnf_name_valid((const uint8_t *)destination_name, destination_length) ||
    destination->record.kind != PNF_INODE_DIRECTORY) {
    return CALL_BAD_REQUEST;
  }
  if (destination->record.cleanup & PNF_CLEANUP_DETACHED) {
    return CALL_NOT_FOUND;
  }
  struct native_store_pool *pool = source->volume->pool;
  enum call_status status = begin_namespace(context, pool, true);
  if (status != CALL_OK) {
    return status;
  }
  struct pnf_dirent source_entry, destination_entry;
  uint64_t source_position, destination_position = 0;
  status = directory_find(context, source, source_name, source_length, &source_entry, &source_position);
  if (status != CALL_OK) {
    return status;
  }
  struct native_store_inode *moved;
  status = inode_get(context, source->volume, source_entry.inode, &moved);
  if (status != CALL_OK) {
    return status;
  }
  if (!valid_child(source, moved)) {
    discard_idle_inode(moved, false);
    return corrupt(context);
  }
  if (moved->record.kind != PNF_INODE_FILE) {
    discard_idle_inode(moved, false);
    return CALL_WRONG_TYPE;
  }
  if (source == destination && source_length == destination_length &&
    !memcmp(source_name, destination_name, source_length)) {
    discard_idle_inode(moved, false);
    return CALL_OK;
  }
  status = directory_find(context, destination, destination_name, destination_length,
    &destination_entry, &destination_position);
  struct native_store_inode *victim = NULL;
  if (status == CALL_OK) {
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
    if (victim->record.kind != PNF_INODE_FILE || victim == moved) {
      if (victim != moved) {
        discard_idle_inode(victim, false);
      }
      discard_idle_inode(moved, false);
      return CALL_WRONG_TYPE;
    }
  } else if (status != CALL_NOT_FOUND) {
    discard_idle_inode(moved, false);
    return status;
  }
  struct pnf_inode source_previous = source->record;
  struct pnf_inode destination_previous = destination->record;
  struct pnf_inode victim_previous = victim ? victim->record : (struct pnf_inode){0};
  struct pnf_volume volume_previous = source->volume->record;
  uint64_t free_previous = pool->free_blocks;
  if (victim) {
    status = directory_clear(context, destination, destination_position, destination_entry.record_length);
    if (status == CALL_OK) {
      status = cleanup_link(context, victim, PNF_CLEANUP_DETACHED);
    }
    if (status == CALL_OK) {
      status = stage_inode(context, victim);
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
  if (status == CALL_OK) {
    status = commit(context, pool);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      source->record = source_previous;
      destination->record = destination_previous;
      source->volume->record = volume_previous;
      if (victim) {
        victim->record = victim_previous;
      }
    }
    namespace_abort(pool, free_previous);
    discard_idle_inode(moved, false);
    if (victim) {
      discard_idle_inode(victim, false);
    }
    return status;
  }
  source->durable = source->record;
  destination->durable = destination->record;
  source->generation = ++next_generation;
  if (destination != source) {
    destination->generation = ++next_generation;
  }
  if (victim) {
    victim->durable = victim->record;
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

static enum call_status highest_mapping(struct native_store_context *context, struct native_store_inode *inode,
  uint64_t limit, struct cleanup_path *path)
{
  struct native_store_pool *pool = inode->volume->pool;
  const uint64_t *pointers = inode->durable.pointers;
  memset(path, 0, sizeof(*path));
  struct search_frame {
    uint64_t block, base, span;
    unsigned level, cursor, child_index;
  } frames[3];
  uint64_t bases[3] = {PNF_DIRECT_COUNT, PNF_DIRECT_COUNT + PNF_INDIRECT_COUNT,
    PNF_DIRECT_COUNT + PNF_INDIRECT_COUNT + (uint64_t)PNF_INDIRECT_COUNT * PNF_INDIRECT_COUNT};
  uint64_t spans[3] = {1, PNF_INDIRECT_COUNT, (uint64_t)PNF_INDIRECT_COUNT * PNF_INDIRECT_COUNT};
  for (unsigned tier = 3; tier; tier--) {
    unsigned root_slot = PNF_DIRECT_COUNT + tier - 1;
    if (!pointers[root_slot]) {
      continue;
    }
    frames[0] = (struct search_frame){pointers[root_slot], bases[tier - 1], spans[tier - 1], tier, PNF_INDIRECT_COUNT, 0};
    unsigned depth = 1;
    while (depth) {
      struct search_frame *frame = &frames[depth - 1];
      enum call_status status = allocated_block(context, pool, frame->block);
      if (status != CALL_OK) {
        return status;
      }
      status = read_block(context, pool, frame->block, pool->io[0]);
      if (status != CALL_OK) {
        return status;
      }
      bool empty = true;
      for (unsigned i = 0; i < PNF_INDIRECT_COUNT; i++) {
        if (pnf_get_u64(pool->io[0] + i * 8)) {
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
        uint64_t block = pnf_get_u64(pool->io[0] + index * 8);
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
          frame->span / PNF_INDIRECT_COUNT, frame->level - 1, PNF_INDIRECT_COUNT, 0};
        descended = true;
        break;
      }
      if (!descended) {
        depth--;
      }
    }
  }
  for (unsigned slot = PNF_DIRECT_COUNT; slot; slot--) {
    unsigned index = slot - 1;
    if (index >= limit && pointers[index]) {
      path->target = pointers[index];
      path->root_slot = index;
      return CALL_OK;
    }
  }
  return CALL_OK;
}

static enum call_status remove_mapping(struct native_store_context *context, struct native_store_inode *inode,
  const struct cleanup_path *path)
{
  struct native_store_pool *pool = inode->volume->pool;
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
    status = edit_block(context, pool, path->parents[index], PNF_METADATA_INDIRECT, false, &bytes);
    if (status != CALL_OK) {
      return status;
    }
    if (pnf_get_u64(bytes + path->indices[index] * 8) != freed) {
      return corrupt(context);
    }
    pnf_put_u64(bytes + path->indices[index] * 8, 0);
    bool empty = true;
    for (unsigned i = 0; i < PNF_INDIRECT_COUNT; i++) {
      if (pnf_get_u64(bytes + i * 8)) {
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

static bool cleanup_mapping_fits(const struct native_store_pool *pool, const struct cleanup_path *path)
{
  uint64_t homes[7];
  unsigned count = 0;
  homes[count++] = pool->header.bitmap_start + path->target / PNF_BITMAP_BITS;
  for (unsigned i = 0; i < path->depth; i++) {
    homes[count++] = path->parents[i];
    homes[count++] = pool->header.bitmap_start + path->parents[i] / PNF_BITMAP_BITS;
  }
  unsigned images = pool->image_count;
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
   * Ancestor bitmap images are included even when no ancestor becomes empty. */
  return images <= STORE_CLEANUP_IMAGES - 2;
}

static void drop_inode_cache(struct native_store_inode *inode, uint64_t first)
{
  struct native_store_pool *pool = inode->volume->pool;
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

static void discard_idle_inode(struct native_store_inode *inode, bool pressure)
{
  if (inode->references || inode->record.cleanup || inode->size_dirty ||
    inode->record.kind == PNF_INODE_FREE || next_dirty(inode)) {
    return;
  }
  if (!pressure) {
    struct native_store_pool *pool = inode->volume->pool;
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
  struct native_store_inode **link = &inode->volume->inodes;
  while (*link != inode) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = inode->next;
  store_free(inode);
}

static enum call_status cleanup_one(struct native_store_context *context, struct native_store_volume *volume)
{
  struct native_store_pool *pool = volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  struct native_store_inode *inode = NULL, *previous = NULL;
  uint64_t number = volume->record.cleanup_head;
  uint64_t count = volume->record.inode_bytes / PNF_INODE_SIZE;
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
    if ((inode->record.cleanup & PNF_CLEANUP_SHRINK) || !inode->references) {
      break;
    }
    previous = inode;
    number = inode->record.cleanup_next;
  }
  if (!number) {
    return CALL_OK;
  }
  status = checkpoint(context, pool);
  if (status != CALL_OK) {
    return status;
  }
  bool detached = (inode->record.cleanup & PNF_CLEANUP_DETACHED) && !inode->references;
  uint64_t target = detached ? 0 : inode->record.shrink_target;
  uint64_t limit = target / PNF_BLOCK_SIZE + (target % PNF_BLOCK_SIZE != 0);
  struct pnf_inode old_record = inode->record, old_durable = inode->durable;
  struct pnf_inode previous_record = previous ? previous->record : (struct pnf_inode){0};
  struct pnf_volume old_volume = volume->record;
  uint64_t old_free = pool->free_blocks;
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
    inode->record.cleanup &= ~PNF_CLEANUP_SHRINK;
    inode->record.shrink_target = 0;
    inode->durable.cleanup &= ~PNF_CLEANUP_SHRINK;
    inode->durable.shrink_target = 0;
    reclaimed = (inode->record.cleanup & PNF_CLEANUP_DETACHED) && !inode->references;
    if (!inode->record.cleanup || reclaimed) {
      uint64_t next = inode->record.cleanup_next;
      if (previous) {
        previous->record.cleanup_next = next;
        struct pnf_inode durable_previous = previous->durable;
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
  if (status == CALL_OK) {
    status = commit(context, pool);
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
    namespace_abort(pool, old_free);
    return status;
  }
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

static enum call_status zero_growth_tail(struct native_store_context *context, struct native_store_inode *inode)
{
  if (!inode->record.size || inode->record.size % PNF_BLOCK_SIZE == 0) {
    return CALL_OK;
  }
  uint64_t logical = inode->record.size / PNF_BLOCK_SIZE;
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
  unsigned offset = (unsigned)(inode->record.size % PNF_BLOCK_SIZE);
  memset(entry->bytes + offset, 0, PNF_BLOCK_SIZE - offset);
  entry->dirty = true;
  return CALL_OK;
}

enum call_status native_store_resize(struct native_store_context *context, struct native_store_inode *inode,
  uint64_t size)
{
  require_owner();
  struct native_store_pool *pool = inode->volume->pool;
  enum call_status status = writable(pool);
  if (status != CALL_OK) {
    return status;
  }
  if (inode->record.kind != PNF_INODE_FILE) {
    return CALL_WRONG_TYPE;
  }
  if (size > PNF_FILE_SIZE_MAX) {
    return CALL_FILE_TOO_LARGE;
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
  status = begin_namespace(context, pool, true);
  if (status != CALL_OK) {
    return status;
  }
  struct pnf_inode old_record = inode->record;
  struct pnf_volume old_volume = inode->volume->record;
  uint64_t old_free = pool->free_blocks;
  inode->record.size = size;
  inode->record.shrink_target = size;
  touch_inode(context, &inode->record, false);
  status = cleanup_link(context, inode, PNF_CLEANUP_SHRINK);
  if (status == CALL_OK) {
    status = stage_inode(context, inode);
  }
  if (status == CALL_OK) {
    status = commit(context, pool);
  }
  if (status != CALL_OK) {
    if (!pool->failed) {
      inode->record = old_record;
      inode->volume->record = old_volume;
    }
    namespace_abort(pool, old_free);
    return status;
  }
  inode->durable = inode->record;
  inode->durable_size = size;
  inode->size_dirty = false;
  drop_inode_cache(inode, size / PNF_BLOCK_SIZE + (size % PNF_BLOCK_SIZE != 0));
  return CALL_OK;
}

bool native_store_pending(const struct native_store_pool *pool)
{
  require_owner();
  if (!pool->writable || pool->failed) {
    return false;
  }
  if (pool->control.state == PNF_JOURNAL_COMMITTED) {
    return true;
  }
  for (const struct native_store_volume *volume = pool->volumes; volume; volume = volume->next) {
    for (const struct native_store_inode *inode = volume->inodes; inode; inode = inode->next) {
      if ((inode->record.cleanup & PNF_CLEANUP_SHRINK) ||
        ((inode->record.cleanup & PNF_CLEANUP_DETACHED) && !inode->references)) {
        return true;
      }
    }
  }
  return false;
}

enum call_status native_store_maintain(struct native_store_context *context, struct native_store_pool *pool,
  bool flush_dirty, bool pressure)
{
  require_owner();
  enum call_status status = pool->failed ? pool->writeback_error : CALL_OK;
  if (pool->writable && !pool->failed && (flush_dirty || pressure)) {
    status = flush_files(context, pool);
  }
  if (pressure) {
    for (struct native_store_volume *volume = pool->volumes; volume; volume = volume->next) {
      struct native_store_inode *inode = volume->inodes;
      while (inode) {
        struct native_store_inode *next = inode->next;
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
  if (pool->control.state == PNF_JOURNAL_COMMITTED) {
    return checkpoint(context, pool);
  }
  for (struct native_store_volume *volume = pool->volumes; volume; volume = volume->next) {
    uint64_t number = volume->record.cleanup_head;
    uint64_t count = volume->record.inode_bytes / PNF_INODE_SIZE;
    while (number) {
      if (!count--) {
        return corrupt(context);
      }
      struct native_store_inode *inode;
      status = inode_get(context, volume, number, &inode);
      if (status != CALL_OK) {
        return status;
      }
      if ((inode->record.cleanup & PNF_CLEANUP_SHRINK) || !inode->references) {
        return cleanup_one(context, volume);
      }
      number = inode->record.cleanup_next;
    }
  }
  return CALL_OK;
}

void native_store_info(struct native_store_inode *inode, struct directory_filesystem_info *info)
{
  require_owner();
  struct native_store_pool *pool = inode->volume->pool;
  memset(info, 0, sizeof(*info));
  info->type = FILESYSTEM_TYPE_PYXIS;
  info->flags = pool->writable ? 0 : FILESYSTEM_FLAG_READ_ONLY;
  if (pool->degraded) {
    info->flags |= FILESYSTEM_FLAG_DEGRADED;
  }
  memcpy(info->pool_id, pool->header.pool_id, PNF_ID_SIZE);
  memcpy(info->volume_id, inode->volume->record.id, PNF_ID_SIZE);
  info->generation = pool->control.sequence;
  info->pool_allocatable_bytes = (pool->header.pool_blocks - 2) * PNF_BLOCK_SIZE;
  memcpy(info->volume_name, inode->volume->record.name, inode->volume->record.name_length);
}
