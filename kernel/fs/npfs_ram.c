#include "npfs_ram.h"
#include <abi/mount.h>
#include <arch/cpu.h>
#include <arch/paging.h>
#include <kernel/fs/metadata.h>
#include <kernel/fs/npfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/task.h>
#include <pyxis_fs/npfs.h>

#define RAM_INDEX_ENTRIES 512u
#define RAM_JOURNAL_BLOCKS 258u

struct ram_index {
  phys_addr_t frames[RAM_INDEX_ENTRIES];
};
struct npfs_ram {
  uint64_t blocks;
  size_t index_count;
  struct ram_index **index;
};

static void *allocate(size_t bytes)
{
  uint64_t flags = cpu_save_interrupts();
  void *memory = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (memory) memset(memory, 0, bytes);
  return memory;
}

static void release(void *memory)
{
  uint64_t flags = cpu_save_interrupts();
  kfree(memory);
  cpu_restore_interrupts(flags);
}

uint64_t npfs_ram_capacity(const struct npfs_ram *ram)
{
  return ram->blocks * NPFS_BLOCK_SIZE;
}

enum call_status npfs_ram_io(struct npfs_ram *ram, enum block_operation operation,
    uint64_t first, uint32_t count, void *bytes, uint64_t deadline)
{
  npfs_require_worker();
  if (operation == BLOCK_FLUSH) return CALL_OK;
  if (!count || first >= ram->blocks || count > ram->blocks - first || !bytes ||
      (operation != BLOCK_READ && operation != BLOCK_WRITE)) return CALL_BAD_REQUEST;
  uint8_t *cursor = bytes;
  for (uint32_t i = 0; i < count; ++i, cursor += NPFS_BLOCK_SIZE) {
    if (task_deadline_expired(deadline)) return CALL_TIMED_OUT;
    uint64_t block = first + i;
    size_t group = block / RAM_INDEX_ENTRIES, entry = block % RAM_INDEX_ENTRIES;
    struct ram_index *index = ram->index[group];
    if (operation == BLOCK_WRITE && !index) {
      index = allocate(sizeof(*index));
      if (!index) return CALL_NO_MEMORY;
      ram->index[group] = index;
    }
    phys_addr_t frame = index ? index->frames[entry] : 0;
    if (!frame && operation == BLOCK_READ) {
      memset(cursor, 0, NPFS_BLOCK_SIZE);
      continue;
    }
    uint64_t flags = cpu_save_interrupts();
    if (!frame) {
      frame = pmm_alloc(1);
      if (!frame) {
        cpu_restore_interrupts(flags);
        return CALL_NO_MEMORY;
      }
      index->frames[entry] = frame;
    }
    /* Full-block writes initialize a newly acquired frame before any read. */
    void *mapped = arch_frame_map(frame);
    if (operation == BLOCK_WRITE) memcpy(mapped, cursor, NPFS_BLOCK_SIZE);
    else memcpy(cursor, mapped, NPFS_BLOCK_SIZE);
    arch_frame_unmap();
    cpu_restore_interrupts(flags);
  }
  return CALL_OK;
}

void npfs_ram_discard(struct npfs_ram *ram)
{
  if (!ram) return;
  npfs_require_worker();
  for (size_t group = 0; group < ram->index_count; ++group) {
    struct ram_index *index = ram->index ? ram->index[group] : NULL;
    if (!index) continue;
    for (size_t i = 0; i < RAM_INDEX_ENTRIES; ++i) {
      if (!index->frames[i]) continue;
      uint64_t flags = cpu_save_interrupts();
      pmm_free(index->frames[i], 1);
      cpu_restore_interrupts(flags);
    }
    release(index);
  }
  release(ram->index);
  release(ram);
}

static enum call_status format(struct npfs_ram *ram, uint64_t deadline)
{
  uint8_t id[NPFS_ID_SIZE] = {'R', 'A', 'M', 'N', 'P', 'F', 'S', 0};
  uint64_t identity;
  uint64_t flags = cpu_save_interrupts();
  bool identified = fs_metadata_allocate_id(&identity);
  cpu_restore_interrupts(flags);
  if (!identified) return CALL_LIMIT;
  memcpy(id + 8, &identity, sizeof(identity));
  struct npfs_header header;
  if (npfs_header_layout(ram->blocks, RAM_JOURNAL_BLOCKS, id, &header) != NPFS_OK)
    return CALL_BAD_REQUEST;
  header.read_only_compatible = NPFS_FEATURE_RO_HARD_LINKS;
  uint8_t *block = allocate(NPFS_BLOCK_SIZE);
  if (!block) return CALL_NO_MEMORY;
  enum call_status status = CALL_OK;
  for (uint64_t page = 0; page < header.bitmap_blocks && status == CALL_OK; ++page) {
    memset(block, 0, NPFS_BLOCK_SIZE);
    for (unsigned bit = 0; bit < NPFS_BITMAP_BITS; ++bit) {
      uint64_t number = page * NPFS_BITMAP_BITS + bit;
      if (!npfs_data_block_valid(&header, number)) block[bit / 8] |= 1u << (bit % 8);
    }
    status = npfs_ram_io(ram, BLOCK_WRITE, header.bitmap_start + page, 1, block, deadline);
  }
  /* Unwritten catalog slots are the codec's all-zero FREE volume records. */
  struct npfs_control control = {.state = NPFS_JOURNAL_EMPTY};
  memcpy(control.pool_id, id, sizeof(id));
  for (unsigned slot = 0; slot < 2 && status == CALL_OK; ++slot) {
    control.sequence = slot;
    if (npfs_control_encode(&header, &control, block) != NPFS_OK) status = CALL_IO;
    else status = npfs_ram_io(ram, BLOCK_WRITE, header.journal_start + slot, 1, block, deadline);
  }
  if (status == CALL_OK && npfs_header_encode(&header, block) != NPFS_OK) status = CALL_IO;
  if (status == CALL_OK) status = npfs_ram_io(ram, BLOCK_WRITE, 0, 1, block, deadline);
  if (status == CALL_OK) status = npfs_ram_io(ram, BLOCK_WRITE, ram->blocks - 1, 1, block, deadline);
  release(block);
  return status;
}

enum call_status npfs_ram_create(uint64_t capacity, uint64_t deadline, struct npfs_ram **out)
{
  npfs_require_worker();
  *out = NULL;
  if (capacity < MOUNT_RAM_MIN_BYTES || capacity > MOUNT_RAM_MAX_BYTES ||
      capacity % NPFS_BLOCK_SIZE) return CALL_BAD_REQUEST;
  struct npfs_ram *ram = allocate(sizeof(*ram));
  if (!ram) return CALL_NO_MEMORY;
  ram->blocks = capacity / NPFS_BLOCK_SIZE;
  ram->index_count = (ram->blocks + RAM_INDEX_ENTRIES - 1) / RAM_INDEX_ENTRIES;
  ram->index = allocate(ram->index_count * sizeof(*ram->index));
  enum call_status status = ram->index ? format(ram, deadline) : CALL_NO_MEMORY;
  if (status != CALL_OK) npfs_ram_discard(ram);
  else *out = ram;
  return status;
}
